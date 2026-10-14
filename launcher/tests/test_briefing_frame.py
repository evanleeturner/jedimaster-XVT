"""The menu frame pictures: composition, the name set, the route.

Purpose:
    Prove the whole-screen picture is composed as the setup screen draws
    it (background with every pixel, pad and activity frame skipping index
    0, a smaller layer leaving the rest), that only the twelve frame names
    are served, and that ``GET /art/frame/<name>`` answers 200, 404 or 403
    as the art route does, trying again after a failed build.

Flow:
    Made-up bitmaps through ``screen_picture`` and ``frame_picture``;
    ``FrameStore`` over a made-up install; an aiohttp test client.

Invariants:
    - No game data; only 127.0.0.1.

Call:
    ``pytest tests/test_briefing_frame.py``
"""

from __future__ import annotations

import logging
from pathlib import Path

import pytest
from bmpdata import bmp
from bmpdata import plain
from bmpdata import ramp
from briefingdata import make_install
from briefingdata import solid_bmp
from pagedata import LAUNCHER_VERSION
from test_briefing_under import picture
from test_briefing_under import png_rgba

from jedimaster.briefing import frame as frame_module
from jedimaster.briefing.frame import BACKGROUNDS
from jedimaster.briefing.frame import frame_art_name
from jedimaster.briefing.frame import FRAME_NAMES
from jedimaster.briefing.frame import FrameStore
from jedimaster.briefing.under import screen_picture
from jedimaster.briefing.under import SCREEN_WIDTH
from jedimaster.icons import color_565
from jedimaster.icons import read_bmp
from jedimaster.page.control import Control
from jedimaster.page.server import make_app
from jedimaster.page.settings import SettingsStore

logger = logging.getLogger(__name__)

KEY = "frame-test-key"
RED = (248, 0, 0)
GREEN = (0, 252, 0)
BLUE = (0, 0, 248)


def at(screen: list[int], x: int, y: int) -> int:
    return screen[y * SCREEN_WIDTH + x]


def test_the_set_is_twelve_frame_names():
    assert len(FRAME_NAMES) == 12 == len(set(FRAME_NAMES))
    assert frame_art_name("gametr") == "frame-gametr.png" == FRAME_NAMES[0]
    assert len(BACKGROUNDS) == 12


def test_the_background_keeps_index_0_and_the_pad_and_frame_skip_it(tmp_path):
    palette = {0: BLUE, 1: RED, 2: GREEN}
    back = picture(tmp_path, "b", 0, {(1, 1): 1}, palette)
    pad = picture(tmp_path, "p", 0, {(2, 2): 2, (1, 1): 0}, palette)
    frame = picture(tmp_path, "f", 0, {(3, 3): 1}, palette)
    screen = screen_picture(back, pad, frame)
    assert len(screen) == 640 * 480
    assert at(screen, 0, 0) == color_565(*BLUE)
    assert at(screen, 1, 1) == color_565(*RED)
    assert at(screen, 2, 2) == color_565(*GREEN)
    assert at(screen, 3, 3) == color_565(*RED)
    assert at(screen, 639, 479) == color_565(*BLUE)


def test_a_smaller_layer_leaves_the_rest_as_it_was(tmp_path):
    palette = [ramp(i) for i in range(256)]
    palette[7] = GREEN
    small = tmp_path / "small.bmp"
    small.write_bytes(
        bmp(634, 472, plain([bytes((7,)) * 634] * 472), compression=0, palette=palette)
    )
    back = picture(tmp_path, "b", 1, None, {1: RED})
    screen = screen_picture(back, None, read_bmp(small))
    assert at(screen, 633, 471) == color_565(*GREEN)
    assert at(screen, 634, 0) == color_565(*RED)
    assert at(screen, 0, 472) == color_565(*RED)


def test_the_frame_goes_over_the_pad(tmp_path):
    palette = {0: BLUE, 1: RED, 2: GREEN}
    back = picture(tmp_path, "b", 0, None, palette)
    pad = picture(tmp_path, "p", 0, {(5, 5): 1, (6, 6): 1}, palette)
    frame = picture(tmp_path, "f", 0, {(5, 5): 2}, palette)
    screen = screen_picture(back, pad, frame)
    assert at(screen, 5, 5) == color_565(*GREEN)
    assert at(screen, 6, 6) == color_565(*RED)


def test_a_layer_bigger_than_the_screen_is_clipped(tmp_path):
    palette = [ramp(i) for i in range(256)]
    palette[7] = GREEN
    palette[8] = RED
    rows = [bytearray(700) for _ in range(500)]
    rows[0][639] = 7
    rows[0][650] = 8
    rows[490][5] = 8
    big = tmp_path / "big.bmp"
    big.write_bytes(
        bmp(700, 500, plain([bytes(r) for r in rows]), compression=0, palette=palette)
    )
    back = picture(tmp_path, "b", 1, None, {1: BLUE})
    screen = screen_picture(back, read_bmp(big), None)
    assert len(screen) == 640 * 480
    assert at(screen, 639, 0) == color_565(*GREEN)
    assert at(screen, 10, 1) == color_565(*BLUE)


def test_a_missing_layer_is_left_out(tmp_path):
    screen = screen_picture(None, None, None)
    assert set(screen) == {0}


@pytest.fixture
def install(tmp_path: Path) -> Path:
    root = make_install(tmp_path / "XvT")
    for n, name in enumerate(BACKGROUNDS):
        (root / "frontres" / f"{name}.BMP").write_bytes(solid_bmp(5 + n))
    return root


def test_the_pad_and_the_frame_are_the_installs_own_files(install):
    palette = {1: RED, 2: GREEN}
    picture(install / "frontres", "BLANKPAD.BMP", 0, {(10, 10): 1}, palette)
    picture(install / "frontres", "ALLACTIV.BMP", 0, {(20, 20): 2}, palette)
    art = FrameStore(install).get("frame-gametr.png")
    assert art is not None
    width, _, rgba = png_rgba(art.data)

    def shown(x: int, y: int) -> int:
        at = 4 * (y * width + x)
        return color_565(*rgba[at : at + 3])

    assert shown(10, 10) == color_565(*RED)
    assert shown(20, 20) == color_565(*GREEN)
    assert shown(50, 50) == color_565(*ramp(5))


def test_every_name_builds_a_640_by_480_picture(install):
    store = FrameStore(install)
    for name in FRAME_NAMES:
        art = store.get(name)
        assert art is not None and art.content_type == "image/png"
        assert png_rgba(art.data)[:2] == (640, 480)


def test_a_built_picture_is_kept(install):
    store = FrameStore(install)
    first = store.get("frame-gametr.png")
    (install / "frontres" / "gametr.BMP").unlink()
    assert store.get("frame-gametr.png") is first


def test_any_other_name_is_nothing(install):
    store = FrameStore(install)
    for name in ("", "frame-gamexx.png", "frame-gametr", "../x", "under-rebel.png"):
        assert store.get(name) is None


def test_no_install_serves_nothing():
    assert all(FrameStore(None).get(name) is None for name in FRAME_NAMES)


def test_a_failed_build_is_none_with_a_warning_and_tried_again(install, caplog):
    store = FrameStore(install)
    path = install / "frontres" / "gamecr.BMP"
    saved = path.read_bytes()
    path.unlink()
    with caplog.at_level(logging.WARNING):
        assert store.get("frame-gamecr.png") is None
    assert "gamecr" in caplog.text
    path.write_bytes(saved)
    assert store.get("frame-gamecr.png") is not None


def test_a_build_that_raises_is_none_with_a_warning_and_tried_again(
    install, caplog, monkeypatch
):
    real = frame_module.frame_picture
    calls = []

    def failing(path: Path, background: str) -> bytes | None:
        calls.append(background)
        if len(calls) == 1:
            raise OSError("disk gone")
        return real(path, background)

    monkeypatch.setattr(frame_module, "frame_picture", failing)
    store = FrameStore(install)
    with caplog.at_level(logging.WARNING):
        assert store.get("frame-gametr.png") is None
    assert "disk gone" in caplog.text
    assert store.get("frame-gametr.png") is not None
    assert calls == ["gametr", "gametr"]


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


@pytest.mark.parametrize("name", FRAME_NAMES)
async def test_each_frame_name_is_served(client, install, name):
    response = await client.get(f"/art/frame/{name}")
    assert response.status == 200
    built = FrameStore(install).get(name)
    assert built is not None
    assert await response.read() == built.data
    assert response.headers["Content-Type"].startswith("image/png")
    assert response.headers["X-Content-Type-Options"] == "nosniff"


@pytest.mark.parametrize(
    "name", ["frame-gamexx.png", "under-rebel.png", "times10.png", "%2e%2e/x", "x"]
)
async def test_any_other_name_is_404(client, name):
    assert (await client.get(f"/art/frame/{name}")).status == 404


async def test_the_frame_route_needs_the_key(aiohttp_client, control, tmp_path):
    page = tmp_path / "dist"
    page.mkdir()
    bare = await aiohttp_client(make_app(control, KEY, page))
    assert (await bare.get("/art/frame/frame-gametr.png")).status == 403


async def test_no_install_answers_404(aiohttp_client, tmp_path):
    store = SettingsStore(tmp_path / "config" / "settings.json")
    page = tmp_path / "dist"
    page.mkdir()
    test_client = await aiohttp_client(
        make_app(Control(None, store, LAUNCHER_VERSION), KEY, page)
    )
    await test_client.get(f"/?key={KEY}", allow_redirects=False)
    assert (await test_client.get("/art/frame/frame-gametr.png")).status == 404


async def test_the_route_tries_again_after_a_failed_build(client, install):
    path = install / "frontres" / "gameci.BMP"
    saved = path.read_bytes()
    path.unlink()
    assert (await client.get("/art/frame/frame-gameci.png")).status == 404
    path.write_bytes(saved)
    assert (await client.get("/art/frame/frame-gameci.png")).status == 200
