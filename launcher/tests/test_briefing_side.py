"""The side button sprites: the name set, the list lines, the picture, the route.

Purpose:
    Prove the 48 side sprite names are the whole set, that a name finds its
    bitmap through ``icons.lst`` and ``side.lst`` (Balance of Power's first,
    comments and trailing spaces ignored, a name only in the base list
    found), that the picture is the bitmap at its own size with index 0
    clear and every other pixel widened as the frame's are, and that
    ``GET /art/side/<name>`` answers 200, 404 or 403 as the frame route does,
    trying again after a failed build.

Flow:
    Made-up bitmaps and lists through ``sprite_png``, ``sprite_files`` and
    ``SideStore``; an aiohttp test client.

Invariants:
    - No game data; only 127.0.0.1.

Call:
    ``pytest tests/test_briefing_side.py``
"""

from __future__ import annotations

import logging
from pathlib import Path

import pytest
from bmpdata import bmp
from bmpdata import end
from bmpdata import eol
from bmpdata import ramp
from bmpdata import run
from briefingdata import make_install
from listdata import crlf
from pagedata import LAUNCHER_VERSION
from test_briefing_under import png_rgba
from textdata import write_files
from write_frame_install import side_files
from write_frame_install import sprite_color

from jedimaster.briefing import side as side_module
from jedimaster.briefing.side import SIDE_NAMES
from jedimaster.briefing.side import SideStore
from jedimaster.briefing.side import sprite_files
from jedimaster.briefing.under import sprite_png
from jedimaster.briefing.under import widen5
from jedimaster.briefing.under import widen6
from jedimaster.icons import color_565
from jedimaster.icons import read_bmp
from jedimaster.page.control import Control
from jedimaster.page.server import make_app
from jedimaster.page.settings import SettingsStore

logger = logging.getLogger(__name__)

KEY = "side-test-key"
RED = (248, 0, 0)
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def expected_names() -> set[str]:
    names = {f"game{n}{look}" for n in (1, 2, 3, 4, 5, 8) for look in "ud"}
    names |= {f"reg{n}{look}" for n in (6, 7) for look in "ud"}
    names |= {f"active{n}" for n in range(1, 9)}
    pieces = "1litb 2lita 2litb 3lita 3litb 4lita 4litb 5lita 5litb 6lita 7litb 8lita"
    names |= {f"{piece}{on}" for piece in pieces.split() for on in "01"}
    return {f"{name}.png" for name in names}


def test_the_set_is_the_48_names():
    assert len(SIDE_NAMES) == 48
    assert set(SIDE_NAMES) == expected_names()


def test_a_run_length_bitmap_is_a_png_at_its_own_size_with_index_0_clear():
    data = run(3, 0) + run(2, 5) + eol() + run(1, 5) + run(4, 0) + end()
    palette = [ramp(i) for i in range(256)]
    palette[5] = (200, 100, 50)
    picture = read_bmp(bmp(5, 2, data, palette=palette))
    width, height, rgba = png_rgba(sprite_png(picture))
    assert (width, height) == (5, 2)
    pixels = [tuple(rgba[at : at + 4]) for at in range(0, len(rgba), 4)]
    packed = color_565(200, 100, 50)
    seen = (
        widen5(packed >> 11),
        widen6((packed >> 5) & 63),
        widen5(packed & 31),
        255,
    )
    assert [p[3] for p in pixels[:5]] == [255, 0, 0, 0, 0]
    assert [p[3] for p in pixels[5:]] == [0, 0, 0, 255, 255]
    opaque = [p for p in pixels if p[3] == 255]
    assert opaque and all(p == seen for p in opaque)
    assert sum(1 for p in pixels if p[3] == 0) == 7


def test_index_0_is_clear_whatever_its_color():
    palette = [ramp(i) for i in range(256)]
    palette[0] = RED
    picture = read_bmp(bmp(2, 1, run(1, 0) + run(1, 1) + end(), palette=palette))
    _, _, rgba = png_rgba(sprite_png(picture))
    assert rgba[3] == 0
    assert rgba[7] == 255


@pytest.fixture
def install(tmp_path: Path) -> Path:
    root = make_install(tmp_path / "XvT")
    write_files(root, side_files())
    return root


def alpha_at(art, x: int, y: int) -> int:
    width, _, rgba = png_rgba(art.data)
    return rgba[4 * (y * width + x) + 3]


def test_every_name_builds_a_png_at_its_own_size(install):
    store = SideStore(install)
    for name in SIDE_NAMES:
        art = store.get(name)
        assert art is not None and art.content_type == "image/png"
        assert png_rgba(art.data)[:2] == (44, 360)


def test_a_sprite_is_its_own_file_with_index_0_clear(install):
    art = SideStore(install).get("game1u.png")
    assert art is not None
    width, _, rgba = png_rgba(art.data)
    inside = 4 * (120 * width + 30)
    color = sprite_color("game1u")
    packed = color_565(*color)
    assert tuple(rgba[inside : inside + 4]) == (
        widen5(packed >> 11),
        widen6((packed >> 5) & 63),
        widen5(packed & 31),
        255,
    )
    assert alpha_at(art, 0, 0) == 0
    assert alpha_at(art, 30, 100) == 0


def test_a_name_only_balance_of_power_lists_is_found_there(install):
    names = ("game8u", "game8d", "active8", "6lita0", "6lita1", "8lita0", "8lita1")
    for name in names:
        words = sprite_files(install, name)
        assert words == [f"frontres\\{name}.bmp"]
        assert SideStore(install).get(f"{name}.png") is not None


def test_a_name_only_in_the_base_list_is_found_there(install):
    assert sprite_files(install, "game1u") == ["frontres\\game1u.bmp"]


def test_balance_of_powers_list_is_read_before_the_base_list(install):
    write_files(
        install,
        {
            "BalanceOfPower/frontres/icons.lst": crlf(
                "header", "frontres\\other.bmp game1u 1"
            ),
            "BalanceOfPower/frontres/other.BMP": (
                install / "frontres" / "active1.BMP"
            ).read_bytes(),
        },
    )
    assert sprite_files(install, "game1u")[0] == "frontres\\other.bmp"
    art = SideStore(install).get("game1u.png")
    assert art is not None
    assert alpha_at(art, 4, 116) == 255
    assert alpha_at(art, 30, 120) == 0


def test_the_base_file_answers_when_balance_of_powers_line_has_no_file(install):
    write_files(
        install,
        {
            "BalanceOfPower/frontres/icons.lst": crlf(
                "header", "frontres\\absent.bmp game1u 1"
            )
        },
    )
    art = SideStore(install).get("game1u.png")
    assert art is not None
    assert alpha_at(art, 30, 120) == 255


def test_comment_lines_and_trailing_spaces_are_ignored(install):
    write_files(
        install,
        {
            "frontres/icons.lst": crlf(
                "// first line is skipped",
                "// a comment   ",
                "frontres\\game2u.bmp game2u 1   ",
                "   // an indented comment",
                "frontres\\game2d.bmp game2d 1 ",
            )
        },
    )
    assert sprite_files(install, "game2u") == ["frontres\\game2u.bmp"]
    assert sprite_files(install, "game2d") == ["frontres\\game2d.bmp"]


def test_a_name_no_list_holds_is_nothing_with_a_warning(install, caplog):
    write_files(install, {"frontres/icons.lst": crlf("header")})
    with caplog.at_level(logging.WARNING):
        assert SideStore(install).get("game1u.png") is None
    assert "game1u" in caplog.text


def test_a_built_picture_is_kept(install):
    store = SideStore(install)
    first = store.get("game1u.png")
    (install / "frontres" / "game1u.BMP").unlink()
    assert store.get("game1u.png") is first


def test_any_other_name_is_nothing(install):
    store = SideStore(install)
    for name in ("", "game1u", "game6u.png", "6litb0.png", "../x", "frame-gametr.png"):
        assert store.get(name) is None


def test_no_install_serves_nothing():
    assert all(SideStore(None).get(name) is None for name in SIDE_NAMES)


def test_a_failed_build_is_none_with_a_warning_and_tried_again(install, caplog):
    store = SideStore(install)
    path = install / "frontres" / "game3u.BMP"
    saved = path.read_bytes()
    path.unlink()
    with caplog.at_level(logging.WARNING):
        assert store.get("game3u.png") is None
    assert "game3u" in caplog.text
    path.write_bytes(saved)
    again = store.get("game3u.png")
    assert again is not None and again.data[:8] == PNG_SIGNATURE


def test_a_build_that_raises_is_none_with_a_warning_and_tried_again(
    install, caplog, monkeypatch
):
    real = side_module.side_picture
    calls = []

    def failing(path: Path, name: str) -> bytes | None:
        calls.append(name)
        if len(calls) == 1:
            raise OSError("disk gone")
        return real(path, name)

    monkeypatch.setattr(side_module, "side_picture", failing)
    store = SideStore(install)
    with caplog.at_level(logging.WARNING):
        assert store.get("game4u.png") is None
    assert "disk gone" in caplog.text
    assert store.get("game4u.png") is not None
    assert calls == ["game4u", "game4u"]


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


@pytest.mark.parametrize("name", SIDE_NAMES)
async def test_each_side_name_is_served(client, install, name):
    response = await client.get(f"/art/side/{name}")
    assert response.status == 200
    built = SideStore(install).get(name)
    assert built is not None
    assert await response.read() == built.data
    assert response.headers["Content-Type"].startswith("image/png")
    assert response.headers["X-Content-Type-Options"] == "nosniff"


@pytest.mark.parametrize(
    "name", ["game6u.png", "frame-gametr.png", "game1u", "%2e%2e/x", "x", "6litb0.png"]
)
async def test_any_other_name_is_404(client, name):
    assert (await client.get(f"/art/side/{name}")).status == 404


async def test_the_side_route_needs_the_key(aiohttp_client, control, tmp_path):
    page = tmp_path / "dist"
    page.mkdir()
    bare = await aiohttp_client(make_app(control, KEY, page))
    assert (await bare.get("/art/side/game1u.png")).status == 403


async def test_no_install_answers_404(aiohttp_client, tmp_path):
    store = SettingsStore(tmp_path / "config" / "settings.json")
    page = tmp_path / "dist"
    page.mkdir()
    test_client = await aiohttp_client(
        make_app(Control(None, store, LAUNCHER_VERSION), KEY, page)
    )
    await test_client.get(f"/?key={KEY}", allow_redirects=False)
    assert (await test_client.get("/art/side/game1u.png")).status == 404


async def test_the_route_tries_again_after_a_failed_build(client, install):
    path = install / "frontres" / "game5d.BMP"
    saved = path.read_bytes()
    path.unlink()
    assert (await client.get("/art/side/game5d.png")).status == 404
    path.write_bytes(saved)
    again = await client.get("/art/side/game5d.png")
    assert again.status == 200
    assert (await again.read())[:8] == PNG_SIGNATURE
