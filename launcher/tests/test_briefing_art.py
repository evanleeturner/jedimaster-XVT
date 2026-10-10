"""The briefing's art: the store, and the route that serves it.

Purpose:
    Prove the fixed set of eleven files is built from a made-up install
    (PNG pictures and WAV sounds), kept once built, never anything else;
    and that ``GET /art/briefing/<name>`` answers each name 200 with the
    right media type, any other name 404, and a request without the run's
    key is refused like every other request.

Flow:
    ``ArtStore`` over the made-up install; an aiohttp test client over
    ``make_app`` for the route.

Invariants:
    - No game data; only 127.0.0.1.

Call:
    ``pytest tests/test_briefing_art.py``
"""

from __future__ import annotations

import logging
import struct
import zlib
from pathlib import Path

import pytest
from briefingdata import make_install
from briefingdata import wav_bytes
from pagedata import LAUNCHER_VERSION

from jedimaster.briefing import ART_NAMES
from jedimaster.briefing import ArtStore
from jedimaster.briefing.art import FONT_ART
from jedimaster.briefing.art import read_font10
from jedimaster.briefing.art import SHEET_ART
from jedimaster.briefing.art import sound_files
from jedimaster.page.control import Control
from jedimaster.page.server import make_app
from jedimaster.page.settings import SettingsStore

logger = logging.getLogger(__name__)

KEY = "art-test-key"
PNG_MARK = b"\x89PNG\r\n\x1a\n"


@pytest.fixture
def install(tmp_path: Path) -> Path:
    return make_install(tmp_path / "XvT")


def png_size(data: bytes) -> tuple[int, int]:
    assert data[:8] == PNG_MARK
    (length,) = struct.unpack(">I", data[8:12])
    assert data[12:16] == b"IHDR"
    width, height = struct.unpack(">II", data[16:24])
    assert (
        zlib.crc32(data[12 : 16 + length])
        == struct.unpack(">I", data[16 + length : 20 + length])[0]
    )
    return width, height


def test_the_set_is_eleven_names():
    assert len(ART_NAMES) == 11 == len(set(ART_NAMES))
    assert ART_NAMES[:6] == (
        "mapicon0.png",
        "mapicon1.png",
        "mapicon2.png",
        "mapicon3.png",
        "mapicon4.png",
        "greyicon.png",
    )
    assert FONT_ART == "times10.png"
    assert [n for n in ART_NAMES if n.endswith(".wav")] == [
        "jewelsound.wav",
        "sfxTarget1.wav",
        "sfxTarget2.wav",
        "sfxText.wav",
    ]


def test_every_name_builds_with_its_media_type(install):
    store = ArtStore(install)
    for name in ART_NAMES:
        art = store.get(name)
        assert art is not None, name
        assert art.content_type == (
            "audio/wav" if name.endswith(".wav") else "image/png"
        )
    assert png_size(store.get("mapicon0.png").data) == (320, 200)
    assert store.get("jewelsound.wav").data == wav_bytes(1)
    assert store.get("sfxText.wav").data == wav_bytes(4)


def test_the_sheet_picture_has_the_sheets_pixels(install):
    from jedimaster.icons import icons_view
    from jedimaster.icons import png_bytes

    sheet = icons_view(install).sheets["mapicon1"]
    assert ArtStore(install).get(SHEET_ART["mapicon1"]).data == png_bytes(sheet.bitmap)


def test_a_built_file_is_kept(install):
    store = ArtStore(install)
    first = store.get("times10.png")
    (install / "TIMES10.ABP").unlink()
    assert store.get("times10.png") is first


def test_any_other_name_is_nothing(install):
    store = ArtStore(install)
    for name in (
        "",
        "mapicon5.png",
        "../TIMES10.ABP",
        "mapicon0",
        "MAPICON0.PNG",
        "x.wav",
    ):
        assert store.get(name) is None


def test_no_install_serves_nothing():
    store = ArtStore(None)
    assert all(store.get(name) is None for name in ART_NAMES)


def test_a_file_that_cannot_be_built_is_none_and_tried_again(install, caplog):
    store = ArtStore(install)
    (install / "SFX" / "t1.wav").unlink()
    with caplog.at_level(logging.WARNING):
        assert store.get("sfxTarget1.wav") is None
    assert "sfxTarget1" in caplog.text
    (install / "SFX" / "t1.wav").write_bytes(wav_bytes(9))
    assert store.get("sfxTarget1.wav").data == wav_bytes(9)


def test_the_sound_list_resolves_each_wav(install):
    files = sound_files(install)
    assert sorted(files) == ["jewelsound", "sfxTarget1", "sfxTarget2", "sfxText"]
    assert files["sfxText"].name == "tx.wav"


def test_font_10_is_none_when_missing_or_broken(install, caplog):
    assert read_font10(install) is not None
    (install / "TIMES10.ABP").write_bytes(b"short")
    with caplog.at_level(logging.WARNING):
        assert read_font10(install) is None
    assert "cannot read" in caplog.text


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


@pytest.mark.parametrize("name", ART_NAMES)
async def test_each_art_name_is_served(client, name):
    response = await client.get(f"/art/briefing/{name}")
    assert response.status == 200
    expected = "audio/wav" if name.endswith(".wav") else "image/png"
    assert response.headers["Content-Type"].startswith(expected)
    assert response.headers["X-Content-Type-Options"] == "nosniff"
    assert len(await response.read()) > 0


@pytest.mark.parametrize("name", ["mapicon5.png", "times12.png", "%2e%2e/x", "x"])
async def test_any_other_name_is_404(client, name):
    assert (await client.get(f"/art/briefing/{name}")).status == 404


async def test_the_art_route_needs_the_key(aiohttp_client, control, tmp_path):
    page = tmp_path / "dist"
    page.mkdir()
    bare = await aiohttp_client(make_app(control, KEY, page))
    assert (await bare.get("/art/briefing/times10.png")).status == 403


async def test_no_install_answers_404_for_every_name(aiohttp_client, tmp_path):
    store = SettingsStore(tmp_path / "config" / "settings.json")
    page = tmp_path / "dist"
    page.mkdir()
    test_client = await aiohttp_client(
        make_app(Control(None, store, LAUNCHER_VERSION), KEY, page)
    )
    await test_client.get(f"/?key={KEY}", allow_redirects=False)
    for name in ART_NAMES:
        assert (await test_client.get(f"/art/briefing/{name}")).status == 404
