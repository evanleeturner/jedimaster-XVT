"""The bot and the page together: a change made in Discord reaches an open page.

Purpose:
    Prove that a setting changed by the bot reaches an open page's socket as
    ``settings.changed``, that a mission picked in Discord reaches it as
    ``page.show_mission``, that both pushes satisfy the schema, that the page
    count decides the "no page is open" note, and that a page that asks for
    the same command gets the same push.

Flow:
    The page's real application under the aiohttp test client; the bot's
    ``Door`` with ``server.broadcast`` as its way to the pages; picks made
    through ``Door.picked``.

Invariants:
    - One ``Control`` is shared by the page and the bot.
    - No connection to Discord.

Call:
    ``pytest tests/test_bot_page.py``
"""

from __future__ import annotations

import json
import logging
from pathlib import Path

import pytest
from botdata import HOST
from botdata import make_bot
from botdata import Talk
from pagedata import check_valid
from pagedata import request

from jedimaster.bot.discord_door import Door
from jedimaster.page.server import broadcast
from jedimaster.page.server import make_app

logger = logging.getLogger(__name__)

KEY = "test-secret-key"


@pytest.fixture
async def rig(aiohttp_client, tmp_path: Path):
    """Return a linked bot sharing its control with a running page application."""
    bot = make_bot(tmp_path)
    bot.link_account(HOST)
    page_dir = tmp_path / "dist"
    page_dir.mkdir()
    client = await aiohttp_client(make_app(bot.control, KEY, page_dir))
    await client.get(f"/?key={KEY}", allow_redirects=False)

    async def deliver(pushes):
        return await broadcast(client.app, pushes)

    bot.door = Door(bot.core, bot.store, deliver)
    bot.client = client
    return bot


async def open_page(client):
    ws = await client.ws_connect(
        "/ws", headers={"Origin": f"http://127.0.0.1:{client.port}"}
    )
    first = json.loads((await ws.receive(timeout=5)).data)
    assert first["event"] == "status"
    return ws


async def next_push(ws) -> dict:
    return check_valid(json.loads((await ws.receive(timeout=5)).data))


async def test_a_setting_changed_by_the_bot_reaches_an_open_page(rig):
    ws = await open_page(rig.client)
    picker = rig.core.settings(HOST, True).pickers[0]
    talk = Talk(HOST)
    await rig.door.picked(talk, picker, ["sharp_bilinear"])
    assert await next_push(ws) == {
        "event": "settings.changed",
        "data": {"settings": {"art_scaling": "sharp_bilinear"}},
    }
    assert talk.texts() == ["Art scaling set to Sharp bilinear."]
    await ws.close()


async def test_every_open_page_gets_the_push(rig):
    first, second = await open_page(rig.client), await open_page(rig.client)
    picker = rig.core.settings(HOST, True).pickers[0]
    await rig.door.picked(Talk(HOST), picker, ["engine_fit"])
    assert await next_push(first) == await next_push(second)
    await first.close()
    await second.close()


async def test_a_mission_picked_in_discord_reaches_an_open_page(rig):
    ws = await open_page(rig.client)
    picker = rig.core.missions(HOST, "training").pickers[0]
    talk = Talk(HOST)
    await rig.door.picked(talk, picker, ["2"])
    assert await next_push(ws) == {
        "event": "page.show_mission",
        "data": {"mission_type": "training", "id": 2, "title": "Second Flight"},
    }
    assert talk.texts() == ["Showing Second Flight on the page."]
    await ws.close()


async def test_a_mission_the_menu_lacks_sends_the_page_nothing(rig):
    ws = await open_page(rig.client)
    picker = rig.core.missions(HOST, "training").pickers[0]
    talk = Talk(HOST)
    await rig.door.picked(talk, picker, ["99"])
    assert "not in the game's list" in talk.texts()[0]
    await ws.send_str(request(1, "settings.get"))
    reply = await next_push(ws)
    assert reply["id"] == 1
    await ws.close()


async def test_with_no_page_open_the_answer_says_so_and_nothing_breaks(rig):
    picker = rig.core.missions(HOST, "training").pickers[0]
    talk = Talk(HOST)
    await rig.door.picked(talk, picker, ["1"])
    assert talk.texts()[0].endswith("No page is open, so nothing changed on screen.")


async def test_the_page_count_is_the_number_of_open_sockets(rig):
    assert await broadcast(rig.client.app, []) == 0
    ws = await open_page(rig.client)
    assert await broadcast(rig.client.app, []) == 1
    await ws.close()


async def test_a_page_and_the_bot_share_one_settings_store(rig):
    ws = await open_page(rig.client)
    await ws.send_str(
        request(4, "settings.set", {"name": "art_scaling", "value": "engine_fit"})
    )
    await next_push(ws)
    await next_push(ws)
    assert "Engine fit" in rig.core.settings(HOST, True).text
    await ws.close()


async def test_a_page_asking_to_show_a_mission_gets_the_same_push(rig):
    ws = await open_page(rig.client)
    args = {"mission_type": "melee", "id": 7}
    await ws.send_str(request(8, "page.show_mission", args))
    reply = await next_push(ws)
    assert reply == {"id": 8, "ok": True, "result": {"shown": True}}
    assert (await next_push(ws))["data"]["title"] == "Open Field"
    await ws.close()
