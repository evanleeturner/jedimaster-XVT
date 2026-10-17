"""The debriefing art: the name set, the lists, the pictures, the route.

Purpose:
    Prove the 79 names are the whole set (32 backgrounds, 44 sprites, two
    font atlases and the fonts' metrics), that a sprite name finds its
    bitmap through the five image lists (Balance of Power's first, comments
    and trailing spaces ignored, a name only in the base list found), that a
    background carries the ``frame``, ``alloff`` and shaded ``regoverlay``
    layers with index 0 clear in every layer but the background, that a
    sprite keeps index 0 clear, and that ``GET /art/debrief/<name>`` answers
    200, 404 or 403 as the frame route does, trying again after a failed
    build.

Flow:
    Made-up files from ``write_frame_install.debrief_files``; ``DebriefStore``
    and an aiohttp test client.

Invariants:
    - No game data; only 127.0.0.1.

Call:
    ``pytest tests/test_briefing_debrief.py``
"""

from __future__ import annotations

import json
import logging
from pathlib import Path

import pytest
from bmpdata import ramp
from briefingdata import make_install
from listdata import crlf
from pagedata import LAUNCHER_VERSION
from test_briefing_under import png_rgba
from textdata import write_files
from write_frame_install import debrief_color
from write_frame_install import debrief_files
from write_frame_install import DEBRIEF_FIRST_COLOR
from write_frame_install import side_files

from jedimaster.briefing import debrief as debrief_module
from jedimaster.briefing.debrief import BACKGROUND_STEMS
from jedimaster.briefing.debrief import DEBRIEF_LISTS
from jedimaster.briefing.debrief import DEBRIEF_NAMES
from jedimaster.briefing.debrief import DebriefStore
from jedimaster.briefing.side import sprite_files
from jedimaster.briefing.under import shade
from jedimaster.briefing.under import widen5
from jedimaster.briefing.under import widen6
from jedimaster.icons import color_565
from jedimaster.page.control import Control
from jedimaster.page.server import make_app
from jedimaster.page.settings import SettingsStore

logger = logging.getLogger(__name__)

KEY = "debrief-test-key"
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def pixel(art, x: int, y: int) -> tuple[int, int, int, int]:
    width, _, rgba = png_rgba(art.data)
    at = 4 * (y * width + x)
    return tuple(rgba[at : at + 4])


def widened(rgb: tuple[int, int, int]) -> tuple[int, int, int]:
    packed = color_565(*rgb)
    return (widen5(packed >> 11), widen6((packed >> 5) & 63), widen5(packed & 31))


@pytest.fixture
def install(tmp_path: Path) -> Path:
    root = make_install(tmp_path / "XvT")
    write_files(root, side_files())
    write_files(root, debrief_files())
    return root


def test_the_set_is_79_names_and_the_32_backgrounds_are_the_game_code_s():
    assert len(DEBRIEF_NAMES) == 79
    assert len(set(DEBRIEF_NAMES)) == 79
    assert len(BACKGROUND_STEMS) == 32


def test_the_background_names_are_the_ones_the_game_code_registers():
    named = set(BACKGROUND_STEMS)
    code = (
        "debcawi debcawr debcali debcalr debtrwi debtrwr debtrbi debtrbr debtrli "
        "debtrlr debtwi debtwr debtbi debtbr debmwi debmwr debmbi debmbr debmli "
        "debmlr debbwr debcwr debbli debcli debbwi debcwi debblr debclr debbti "
        "debcti debbtr debctr"
    ).split()
    assert named == set(code)
    assert len(code) == 32


def test_the_other_names_are_sprites_fonts_and_metrics():
    others = set(DEBRIEF_NAMES) - {f"{s}.png" for s in BACKGROUND_STEMS}
    assert {"debrief1u.png", "debrief2d.png", "deb4u.png", "leaveup.png"} <= others
    assert {"imptiny0.png", "imptiny15.png", "rebtiny0.png", "rebtiny15.png"} <= others
    assert {"times12.png", "times15.png", "fonts.json"} <= others
    assert "imptiny16.png" not in others


def test_every_name_builds(install):
    store = DebriefStore(install)
    for name in DEBRIEF_NAMES:
        art = store.get(name)
        assert art is not None, name
        expected = "application/json" if name.endswith(".json") else "image/png"
        assert art.content_type == expected


def test_a_background_is_the_screen_with_the_layers_over_it(install):
    art = DebriefStore(install).get("debcwi.png")
    assert art is not None
    assert png_rgba(art.data)[:2] == (640, 480)
    index = DEBRIEF_FIRST_COLOR + BACKGROUND_STEMS.index("debcwi")
    far = pixel(art, 300, 300)
    assert far == (*widened(ramp(index)), 255)
    # frame's block at (0, 0): its own color, index 0 elsewhere leaves the ground
    assert pixel(art, 1, 1)[:3] == widened((250, 40, 40))
    assert pixel(art, 12, 12)[:3] == widened((210, 40, 100))
    assert pixel(art, 5, 5)[:3] == pixel(art, 300, 300)[:3]


def test_the_overlay_is_shaded_over_the_screen_not_laid(install):
    art = DebriefStore(install).get("debcwi.png")
    assert art is not None
    index = DEBRIEF_FIRST_COLOR + BACKGROUND_STEMS.index("debcwi")
    under = color_565(*ramp(index))
    over = color_565(170, 40, 160)
    mixed = shade(under, over)
    seen = pixel(art, 21, 21)
    assert seen[:3] == (
        widen5(mixed >> 11),
        widen6((mixed >> 5) & 63),
        widen5(mixed & 31),
    )
    assert pixel(art, 40, 40)[:3] == pixel(art, 300, 300)[:3]


def test_a_missing_layer_is_left_out(install):
    for name in ("FRAME", "ALLOFF", "REGOVERLAY"):
        (install / "frontres" / f"{name.lower()}.BMP").unlink()
    art = DebriefStore(install).get("debcwi.png")
    assert art is not None
    assert pixel(art, 1, 1)[:3] == pixel(art, 300, 300)[:3]


def test_a_sprite_is_its_own_file_with_index_0_clear(install):
    art = DebriefStore(install).get("debrief1u.png")
    assert art is not None
    # A strip from the screen's corner to the tab's bottom right, at its own size.
    assert png_rgba(art.data)[:2] == (42, 138)
    assert pixel(art, 0, 0)[3] == 0
    assert pixel(art, 21, 113)[3] == 0
    assert pixel(art, 32, 126) == (*widened(debrief_color("debrief1u")), 255)


def test_a_name_in_the_base_list_only_is_found_there(install):
    assert sprite_files(install, "debrief2d", DEBRIEF_LISTS) == [
        "frontres\\debrief2d.bmp"
    ]
    assert sprite_files(install, "flyup", DEBRIEF_LISTS) == ["frontres\\flyup.bmp"]


def test_balance_of_powers_list_is_read_before_the_base_list(install):
    words = sprite_files(install, "leaveup", DEBRIEF_LISTS)
    assert words == ["frontres\\leave2.bmp", "frontres\\leaveup.bmp"]
    art = DebriefStore(install).get("leaveup.png")
    assert art is not None
    assert png_rgba(art.data)[:2] == (176, 471)
    assert pixel(art, 125, 459) == (*widened((9, 99, 199)), 255)


def test_comment_lines_and_trailing_spaces_are_ignored(install):
    write_files(
        install,
        {
            "frontres/awards.lst": crlf(
                "first line is skipped",
                "// frontres\\debrief4u.bmp debrief4u 1",
                "frontres\\debrief4d.bmp debrief4d 1   ",
            )
        },
    )
    assert sprite_files(install, "debrief4u", DEBRIEF_LISTS) == [
        "frontres\\debrief4u.bmp"
    ]
    assert sprite_files(install, "debrief4d", DEBRIEF_LISTS)[0] == (
        "frontres\\debrief4d.bmp"
    )


def test_a_name_no_list_holds_is_nothing_with_a_warning(install, caplog):
    for list_name in ("top", "promo"):
        write_files(install, {f"frontres/{list_name}.lst": crlf("header")})
    write_files(install, {"BalanceOfPower/frontres/top.lst": crlf("header")})
    with caplog.at_level(logging.WARNING):
        assert DebriefStore(install).get("debrief1u.png") is None
    assert "debrief1u" in caplog.text


def test_the_fonts_are_atlases_and_metrics(install):
    store = DebriefStore(install)
    for name in ("times12.png", "times15.png"):
        art = store.get(name)
        assert art is not None and art.data[:8] == PNG_SIGNATURE
    data = store.get("fonts.json")
    assert data is not None
    fonts = json.loads(data.data)
    assert set(fonts) == {"font12", "font15"}
    assert fonts["font15"]["picture"] == "times15.png"
    assert fonts["font15"]["height"] == 10
    assert len(fonts["font12"]["widths"]) == 256


def test_one_font_alone_is_still_metrics(install):
    (install / "TIMES15.ABP").unlink()
    data = DebriefStore(install).get("fonts.json")
    assert data is not None
    assert set(json.loads(data.data)) == {"font12"}


def test_no_font_is_no_metrics(install, caplog):
    (install / "TIMES15.ABP").unlink()
    (install / "TIMES12.ABP").unlink()
    with caplog.at_level(logging.WARNING):
        assert DebriefStore(install).get("fonts.json") is None
    assert "times1" in caplog.text


def test_a_built_picture_is_kept(install):
    store = DebriefStore(install)
    first = store.get("debrief2u.png")
    (install / "frontres" / "debrief2u.BMP").unlink()
    assert store.get("debrief2u.png") is first


def test_any_other_name_is_nothing(install):
    store = DebriefStore(install)
    for name in ("", "debcwi", "debtli.png", "imptiny16.png", "../x", "game1u.png"):
        assert store.get(name) is None


def test_no_install_serves_nothing():
    assert all(DebriefStore(None).get(name) is None for name in DEBRIEF_NAMES)


def test_a_failed_build_is_none_with_a_warning_and_tried_again(install, caplog):
    store = DebriefStore(install)
    path = install / "frontres" / "debcti.BMP"
    saved = path.read_bytes()
    path.unlink()
    with caplog.at_level(logging.WARNING):
        assert store.get("debcti.png") is None
    assert "debcti" in caplog.text
    path.write_bytes(saved)
    again = store.get("debcti.png")
    assert again is not None and again.data[:8] == PNG_SIGNATURE


def test_a_build_that_raises_is_none_with_a_warning_and_tried_again(
    install, caplog, monkeypatch
):
    real = debrief_module.background_picture
    calls = []

    def failing(path: Path, stem: str) -> bytes | None:
        calls.append(stem)
        if len(calls) == 1:
            raise OSError("disk gone")
        return real(path, stem)

    monkeypatch.setattr(debrief_module, "background_picture", failing)
    store = DebriefStore(install)
    with caplog.at_level(logging.WARNING):
        assert store.get("debmwi.png") is None
    assert "disk gone" in caplog.text
    assert store.get("debmwi.png") is not None
    assert calls == ["debmwi", "debmwi"]


@pytest.fixture
def control(tmp_path: Path, install: Path) -> Control:
    store = SettingsStore(tmp_path / "config" / "settings.json")
    return Control(install, store, LAUNCHER_VERSION)


@pytest.fixture
async def client(aiohttp_client, control, tmp_path):
    page = tmp_path / "dist"
    page.mkdir()
    (page / "index.html").write_text("<!doctype html>\n", encoding="utf-8")
    test_client = await aiohttp_client(make_app(control, KEY, page))
    assert (await test_client.get(f"/?key={KEY}", allow_redirects=False)).status == 302
    return test_client


@pytest.mark.parametrize("name", DEBRIEF_NAMES)
async def test_each_name_is_served(client, install, name):
    response = await client.get(f"/art/debrief/{name}")
    assert response.status == 200
    built = DebriefStore(install).get(name)
    assert built is not None
    assert await response.read() == built.data
    assert response.headers["Content-Type"].startswith(built.content_type)
    assert response.headers["X-Content-Type-Options"] == "nosniff"


@pytest.mark.parametrize(
    "name", ["debtli.png", "frame-gametr.png", "debcwi", "%2e%2e/x", "x", "game1u.png"]
)
async def test_any_other_name_is_404(client, name):
    assert (await client.get(f"/art/debrief/{name}")).status == 404


async def test_the_debrief_route_needs_the_key(aiohttp_client, control, tmp_path):
    page = tmp_path / "dist"
    page.mkdir()
    bare = await aiohttp_client(make_app(control, KEY, page))
    assert (await bare.get("/art/debrief/debcwi.png")).status == 403


async def test_no_install_answers_404(aiohttp_client, tmp_path):
    store = SettingsStore(tmp_path / "config" / "settings.json")
    page = tmp_path / "dist"
    page.mkdir()
    test_client = await aiohttp_client(
        make_app(Control(None, store, LAUNCHER_VERSION), KEY, page)
    )
    await test_client.get(f"/?key={KEY}", allow_redirects=False)
    assert (await test_client.get("/art/debrief/debcwi.png")).status == 404


async def test_the_route_tries_again_after_a_failed_build(client, install):
    path = install / "frontres" / "debclr.BMP"
    saved = path.read_bytes()
    path.unlink()
    assert (await client.get("/art/debrief/debclr.png")).status == 404
    path.write_bytes(saved)
    again = await client.get("/art/debrief/debclr.png")
    assert again.status == 200
    assert (await again.read())[:8] == PNG_SIGNATURE
