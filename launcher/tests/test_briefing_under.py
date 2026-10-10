"""What lies under the briefing map: the background rule and the saved picture.

Purpose:
    Prove the rule that picks a team's background from the mission's
    directory, the five layers of the saved picture (index 0 kept for the
    background and skipped for the others, the shading's rounding where
    blue differs from red and green, the caption band's first and last
    rows and columns), the widening of 16-bit pixels to 8 bits and back,
    and that the bundle names each team's background and carries the
    heading and its font.

Flow:
    Made-up bitmaps are written to a folder and read with the game's bitmap
    reader; ``saved_picture`` lays them over the panel; the PNG is read back
    with ``zlib`` and packed again.

Invariants:
    - No game data: every picture, color and string is made up.

Call:
    ``pytest tests/test_briefing_under.py``
"""

from __future__ import annotations

import logging
import struct
import zlib
from pathlib import Path

import pytest
from bmpdata import bmp
from bmpdata import plain
from bmpdata import ramp
from briefingdata import BACKGROUND_INDEX
from briefingdata import briefing_points
from briefingdata import flagged
from briefingdata import HEADING_WORD
from briefingdata import make_install
from briefingdata import mission_bytes
from briefingdata import point

from jedimaster.briefing import ArtStore
from jedimaster.briefing import build_bundle
from jedimaster.briefing import directory_of
from jedimaster.briefing.under import background_for
from jedimaster.briefing.under import BAND_FIRST_ROW
from jedimaster.briefing.under import BAND_LAST_ROW
from jedimaster.briefing.under import PANEL_HEIGHT
from jedimaster.briefing.under import PANEL_LEFT
from jedimaster.briefing.under import PANEL_TOP
from jedimaster.briefing.under import PANEL_WIDTH
from jedimaster.briefing.under import saved_picture
from jedimaster.briefing.under import shade
from jedimaster.briefing.under import under_art_name
from jedimaster.briefing.under import widen5
from jedimaster.briefing.under import widen6
from jedimaster.briefing.under import widened_png
from jedimaster.icons import BmpFile
from jedimaster.icons import color_565
from jedimaster.icons import read_bmp
from jedimaster.mission import read_mission

logger = logging.getLogger(__name__)

BLUE = 0x001F


def picture(
    folder: Path,
    name: str,
    fill: int,
    spots: dict[tuple[int, int], int] | None = None,
    palette: dict[int, tuple[int, int, int]] | None = None,
) -> BmpFile:
    """Write and read back a 640 by 480 bitmap filled with ``fill`` plus ``spots``."""
    rows = [bytearray((fill,)) * 640 for _ in range(480)]
    for (x, y), index in (spots or {}).items():
        rows[y][x] = index
    colors = [ramp(i) for i in range(256)]
    for index, rgb in (palette or {}).items():
        colors[index] = rgb
    data = bmp(640, 480, plain([bytes(r) for r in rows]), compression=0, palette=colors)
    path = folder / name
    path.write_bytes(data)
    return read_bmp(path)


def at(panel: list[int], x: int, y: int) -> int:
    return panel[y * PANEL_WIDTH + x]


def screen(x: int, y: int) -> tuple[int, int]:
    """Return the bitmap position of panel pixel (x, y)."""
    return PANEL_LEFT + x, PANEL_TOP + y


def pack(rgb: tuple[int, int, int]) -> int:
    return color_565(*rgb)


# ---- the background rule ---------------------------------------------------


@pytest.mark.parametrize("directory", ["melee", "tournament"])
@pytest.mark.parametrize("team", [0, 1, 7])
def test_melee_and_tournament_take_the_neutral_background(directory, team):
    assert background_for(directory, team, [1, 0]) == "neutral"


@pytest.mark.parametrize("directory", ["combat", "battle"])
def test_combat_and_battle_are_imperial_for_team_0_and_rebel_for_any_other(directory):
    assert background_for(directory, 0, [0]) == "imperial"
    assert background_for(directory, 1, [1]) == "rebel"
    assert background_for(directory, 9, [None]) == "rebel"


@pytest.mark.parametrize("directory", ["training", "campaign", None])
def test_other_directories_take_the_first_player_group_by_its_iff(directory):
    assert background_for(directory, 0, [None, 0, 1]) == "rebel"
    assert background_for(directory, 0, [None, 1, 0]) == "imperial"
    assert background_for(directory, 5, [None, 2]) == "neutral"
    assert background_for(directory, 5, [None, 200]) == "neutral"


@pytest.mark.parametrize("directory", ["training", None])
def test_with_no_player_group_the_background_is_neutral(directory):
    assert background_for(directory, 0, []) == "neutral"
    assert background_for(directory, 0, [None, None]) == "neutral"


def test_the_team_does_not_change_the_background_of_a_training_mission():
    kinds = {background_for("training", team, [None, 1, 0]) for team in range(10)}
    assert kinds == {"imperial"}


def test_a_directory_is_found_from_the_files_folder_in_any_case():
    base = Path("/x")
    assert directory_of(base / "Train" / "A.TIE") == "training"
    assert directory_of(base / "BalanceOfPower" / "TRAIN" / "a.tie") == "training"
    assert directory_of(base / "Tourn" / "A.TIE") == "tournament"
    assert directory_of(base / "COMBAT" / "A.TIE") == "combat"
    assert directory_of(base / "Battle" / "A.TIE") == "battle"
    assert directory_of(base / "Melee" / "A.TIE") == "melee"
    assert directory_of(base / "CAMPAIGN" / "A.TIE") == "campaign"
    assert directory_of(base / "elsewhere" / "A.TIE") is None


def test_the_art_name_of_a_background():
    assert under_art_name("rebel") == "under-rebel.png"


# ---- the shading -----------------------------------------------------------


def test_shading_halves_the_sum_of_red_and_green_rounding_down():
    assert shade(0b00011 << 11, 0b00010 << 11) == 0b00010 << 11
    assert shade(0b000011 << 5, 0b000010 << 5) == 0b000010 << 5


def test_shading_blue_halves_each_before_adding():
    assert shade(1, 1) == 0
    assert shade(3, 3) == 2
    assert shade(31, 31) == 30
    assert shade(0b00011 << 11, 0b00011 << 11) == 0b00011 << 11


def test_shading_with_blue_keeps_half_of_each_channel():
    assert shade(0xFFFF, BLUE) == (31 + 0) // 2 << 11 | (63 // 2) << 5 | (15 + 15)


# ---- the saved picture ------------------------------------------------------


def layers(folder: Path, **spots) -> list[BmpFile]:
    palette = {1: (248, 0, 0), 2: (0, 252, 0), 3: (0, 0, 248), 4: (8, 8, 8)}
    names = ("background", "pad", "frame", "shading")
    fills = (1, 0, 0, 0)
    return [
        picture(folder, n, f, spots.get(n), palette)
        for n, f in zip(names, fills, strict=True)
    ]


def test_the_background_keeps_every_pixel_index_0_included(tmp_path):
    palette = {0: (0, 0, 248), 1: (248, 0, 0)}
    back = picture(tmp_path, "b", 1, {screen(5, 5): 0}, palette)
    panel = saved_picture(back, None, None, None)
    assert at(panel, 5, 5) == pack((0, 0, 248))
    assert at(panel, 6, 5) == pack((248, 0, 0))


def test_the_pad_and_the_frame_skip_index_0_and_the_shading_skips_it_too(tmp_path):
    back, pad, frame, shading = layers(
        tmp_path,
        pad={screen(1, 1): 2, screen(2, 1): 0},
        frame={screen(3, 1): 3, screen(1, 1): 4},
        shading={screen(5, 1): 2, screen(6, 1): 0},
    )
    panel = saved_picture(back, pad, frame, shading)
    red = pack((248, 0, 0))
    assert at(panel, 2, 1) == red
    assert at(panel, 1, 1) == pack((8, 8, 8))
    assert at(panel, 3, 1) == pack((0, 0, 248))
    assert at(panel, 6, 1) == red
    assert at(panel, 5, 1) == shade(red, pack((0, 252, 0)))


def test_the_layers_go_in_order_background_pad_frame_shading(tmp_path):
    spot = {screen(7, 7): 2}
    back, pad, frame, shading = layers(
        tmp_path, pad=spot, frame={screen(7, 7): 3}, shading={screen(7, 7): 4}
    )
    panel = saved_picture(back, pad, frame, shading)
    assert at(panel, 7, 7) == shade(pack((0, 0, 248)), pack((8, 8, 8)))


def test_a_solid_first_layer_shows_everywhere_outside_the_band(tmp_path):
    back = picture(tmp_path, "b", 1, None, {1: (248, 0, 0)})
    panel = saved_picture(back, None, None, None)
    red = pack((248, 0, 0))
    for y in (0, 100, BAND_FIRST_ROW - 1, BAND_LAST_ROW + 1, PANEL_HEIGHT - 1):
        assert {at(panel, x, y) for x in (0, 1, 180, PANEL_WIDTH - 1)} == {red}


def test_the_band_covers_its_first_and_last_rows_and_columns(tmp_path):
    back = picture(tmp_path, "b", 1, None, {1: (248, 0, 0)})
    panel = saved_picture(back, None, None, None)
    red = pack((248, 0, 0))
    assert (BAND_FIRST_ROW, BAND_LAST_ROW) == (209, 236)
    shaded = shade(red, 0x001F)
    assert shaded == 0x780F
    assert shaded != red
    for x in (0, PANEL_WIDTH - 1):
        assert at(panel, x, 209) == shaded
        assert at(panel, x, 236) == shaded
        assert at(panel, x, 208) == red
        assert at(panel, x, 237) == red


def test_the_panel_is_the_part_of_the_picture_at_84_96(tmp_path):
    back = picture(tmp_path, "b", 1, {(83, 96): 2, (84, 95): 2, (84, 96): 3}, None)
    panel = saved_picture(back, None, None, None)
    assert at(panel, 0, 0) == color_565(*ramp(3))
    assert at(panel, 1, 0) == color_565(*ramp(1))


def test_a_smaller_layer_covers_only_its_own_part_of_the_panel(tmp_path):
    back = picture(tmp_path, "b", 1, None, None)
    small = read_bmp(
        _write(
            tmp_path,
            "s.bmp",
            bmp(200, 200, plain([bytes((2,)) * 200] * 200), compression=0),
        )
    )
    panel = saved_picture(back, small, None, None)
    drawn, kept = color_565(*ramp(2)), color_565(*ramp(1))
    assert [at(panel, 0, 0), at(panel, 115, 103)] == [drawn, drawn]
    assert [at(panel, 116, 5), at(panel, 5, 104), at(panel, 200, 200)] == [kept] * 3


def _write(folder: Path, name: str, data: bytes) -> Path:
    path = folder / name
    path.write_bytes(data)
    return path


def test_a_layer_that_is_not_given_is_left_out(tmp_path):
    panel = saved_picture(None, None, None, None)
    assert at(panel, 10, 10) == 0
    assert at(panel, 10, BAND_FIRST_ROW) == shade(0, BLUE)


# ---- widening --------------------------------------------------------------


def test_widened_channels_pack_back_to_themselves():
    assert [widen5(v) >> 3 for v in range(32)] == list(range(32))
    assert [widen6(v) >> 2 for v in range(64)] == list(range(64))
    assert widen5(31) == 255 and widen6(63) == 255 and widen5(0) == 0
    assert widen5(16) == 132 and widen6(33) == 134


def png_rgba(data: bytes) -> tuple[int, int, bytes]:
    width, height = struct.unpack(">II", data[16:24])
    pos, idat = 8, b""
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        if data[pos + 4 : pos + 8] == b"IDAT":
            idat += data[pos + 8 : pos + 8 + length]
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = 4 * width
    rows = [raw[y * (stride + 1) + 1 : (y + 1) * (stride + 1)] for y in range(height)]
    return width, height, b"".join(rows)


def test_every_16_bit_pixel_survives_the_png_round_trip():
    panel = [i % 65536 for i in range(PANEL_WIDTH * PANEL_HEIGHT)]
    width, height, rgba = png_rgba(widened_png(panel))
    assert (width, height) == (PANEL_WIDTH, PANEL_HEIGHT)
    got = [color_565(*rgba[i : i + 3]) for i in range(0, len(rgba), 4)]
    assert got == panel
    assert set(rgba[3::4]) == {255}


def test_a_png_of_the_wrong_size_is_refused():
    with pytest.raises(ValueError, match="panel"):
        widened_png([0] * 10)


# ---- the art store and the bundle ------------------------------------------


@pytest.fixture
def install(tmp_path: Path) -> Path:
    return make_install(tmp_path / "XvT")


def test_each_background_picture_is_the_panel_in_that_backgrounds_color(install):
    store = ArtStore(install)
    for kind, name in (
        ("PLAYERR", "rebel"),
        ("PLAYERI", "imperial"),
        ("PLAYER", "neutral"),
    ):
        art = store.get(under_art_name(name))
        assert art is not None and art.content_type == "image/png"
        width, height, rgba = png_rgba(art.data)
        assert (width, height) == (PANEL_WIDTH, PANEL_HEIGHT)
        first = color_565(*rgba[0:3])
        assert first == color_565(*ramp(BACKGROUND_INDEX[kind]))
        band = 4 * (BAND_FIRST_ROW * PANEL_WIDTH)
        assert color_565(*rgba[band : band + 3]) == shade(first, BLUE)


@pytest.mark.parametrize(
    "missing", ["PLAYERR.BMP", "BLANKPAD.BMP", "ALLACTIV.BMP", "MAP.BMP"]
)
def test_a_picture_that_cannot_be_built_is_none_with_a_warning(
    install, caplog, missing
):
    (install / "frontres" / missing).unlink()
    with caplog.at_level(logging.WARNING):
        assert ArtStore(install).get("under-rebel.png") is None
    assert missing.lower() in caplog.text.lower()
    assert (
        ArtStore(install).get("under-imperial.png") is not None
        or missing != "PLAYERR.BMP"
    )


def test_balance_of_powers_picture_is_read_first(install):
    folder = install / "BalanceOfPower" / "FRONTRES"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "playerr.bmp").write_bytes(
        bmp(640, 480, plain([bytes((40,)) * 640] * 480), compression=0)
    )
    _, _, rgba = png_rgba(ArtStore(install).get("under-rebel.png").data)
    assert color_565(*rgba[0:3]) == color_565(*ramp(40))


def bundle_for(install: Path, directory, groups, team_flags=(0, 1, 2)):
    brief = {"viewed_by_team": flagged(*team_flags)}
    data = mission_bytes(groups, [brief], ["Red", "Blue", "Gold"])
    return build_bundle(install, read_mission(data), directory)


def group(iff: int, player: int) -> dict:
    return {
        "iff": iff,
        "player_number": player,
        "waypoints": briefing_points(point(0, 0)),
    }


def test_each_team_names_the_background_its_directory_gives(install):
    groups = [group(0, 0), group(1, 1)]
    names = [t["background"] for t in bundle_for(install, "training", groups)["teams"]]
    assert names == ["under-imperial.png"] * 10
    names = [t["background"] for t in bundle_for(install, "combat", groups)["teams"]]
    assert names == ["under-imperial.png"] + ["under-rebel.png"] * 9
    names = [t["background"] for t in bundle_for(install, "melee", groups)["teams"]]
    assert names == ["under-neutral.png"] * 10


def test_a_mission_with_no_player_group_is_neutral(install):
    teams = bundle_for(install, "training", [group(0, 0)])["teams"]
    assert teams[0]["background"] == "under-neutral.png"


def test_no_directory_uses_the_first_player_group(install):
    teams = bundle_for(install, None, [group(1, 0), group(0, 2), group(1, 1)])["teams"]
    assert teams[0]["background"] == "under-rebel.png"


def test_the_heading_is_front_string_207_and_font_12_comes_with_it(install):
    bundle = bundle_for(install, "training", [])
    assert bundle["heading"] == HEADING_WORD
    font = bundle["heading_font"]
    assert font["picture"] == "times12.png"
    assert (font["height"], font["spacing"]) == (8, 1)
    assert font["widths"][65] == 6 and len(font["widths"]) == 256
    assert font["text_colors"] == bundle["font"]["text_colors"]


def test_a_missing_font_12_leaves_the_heading_font_out(install, caplog):
    (install / "TIMES12.ABP").unlink()
    with caplog.at_level(logging.WARNING):
        bundle = bundle_for(install, "training", [])
    assert bundle["heading_font"] is None
    assert "times12" in caplog.text.lower()


def test_a_missing_heading_string_is_empty(install):
    (install / "FRONTTXT.TXT").unlink()
    assert bundle_for(install, "training", [])["heading"] == ""
