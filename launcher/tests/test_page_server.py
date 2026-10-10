"""The web server: the guards, the socket, two pages in step.

Purpose:
    Prove the page and the socket are served only to a request that has the
    run's secret, names this server in its Host header and (for the socket)
    comes from the page's own origin; that the security headers are on
    every response; that an oversize message closes the socket; and that a
    setting changed on one page reaches a second.

Flow:
    An aiohttp test client over ``make_app`` with a made-up install, a
    temporary settings file and a one-file page folder. Every message
    received from the socket is checked against the control schema.

Invariants:
    - No game data; no browser.
    - Only 127.0.0.1 is used.

Call:
    ``pytest tests/test_page_server.py``
"""

from __future__ import annotations

import asyncio
import json
import logging
from pathlib import Path

import aiohttp
import pytest
from pagedata import check_valid
from pagedata import LAUNCHER_VERSION
from pagedata import make_install
from pagedata import request

from jedimaster.page import server
from jedimaster.page.control import Control
from jedimaster.page.protocol import MAX_MESSAGE_BYTES
from jedimaster.page.server import COOKIE_NAME
from jedimaster.page.server import make_app
from jedimaster.page.server import serve
from jedimaster.page.server import SOCKETS
from jedimaster.page.settings import SettingsStore

logger = logging.getLogger(__name__)

KEY = "test-secret-key"
PAGE = "<!doctype html><title>page</title>\n"


@pytest.fixture
def page_dir(tmp_path: Path) -> Path:
    """Return a page folder holding an index and one script."""
    folder = tmp_path / "dist"
    folder.mkdir()
    (folder / "index.html").write_text(PAGE, encoding="utf-8")
    (folder / "main.js").write_text("export {};\n", encoding="utf-8")
    return folder


@pytest.fixture
def control(tmp_path: Path) -> Control:
    store = SettingsStore(tmp_path / "config" / "settings.json")
    return Control(make_install(tmp_path), store, LAUNCHER_VERSION)


@pytest.fixture
async def client(aiohttp_client, control, page_dir):
    """Return a test client, already holding the cookie."""
    test_client = await aiohttp_client(make_app(control, KEY, page_dir))
    response = await test_client.get(f"/?key={KEY}", allow_redirects=False)
    assert response.status == 302
    return test_client


@pytest.fixture
async def bare(aiohttp_client, control, page_dir):
    """Return a test client that has not presented the key."""
    return await aiohttp_client(make_app(control, KEY, page_dir))


def origin_of(client) -> str:
    return f"http://127.0.0.1:{client.port}"


async def connect(client, **headers):
    headers.setdefault("Origin", origin_of(client))
    return await client.ws_connect("/ws", headers=headers)


async def receive(ws) -> dict:
    """Return the next message, checked against the schema."""
    return check_valid(json.loads((await ws.receive(timeout=5)).data))


async def ask(ws, ident, command, args=None) -> dict:
    await ws.send_str(request(ident, command, args))
    return await receive(ws)


# ---- the secret -----------------------------------------------------------


async def test_key_sets_a_strict_cookie_and_redirects(bare):
    response = await bare.get(f"/?key={KEY}", allow_redirects=False)
    assert response.status == 302
    assert response.headers["Location"] == "/"
    cookie = response.cookies[COOKIE_NAME]
    assert cookie.value == KEY
    assert cookie["httponly"] and cookie["samesite"] == "Strict"
    assert cookie["path"] == "/"


async def test_page_is_served_after_the_redirect(bare):
    response = await bare.get(f"/?key={KEY}")
    assert response.status == 200
    assert await response.text() == PAGE
    again = await bare.get("/")
    assert again.status == 200


async def test_wrong_key_is_refused_and_sets_no_cookie(bare, caplog):
    with caplog.at_level(logging.WARNING):
        response = await bare.get("/?key=wrong", allow_redirects=False)
    assert response.status == 403
    assert COOKIE_NAME not in response.cookies
    assert "wrong key" in caplog.text
    assert (await bare.get("/")).status == 403


@pytest.mark.parametrize("path", ["/", "/main.js", "/ws", "/missing"])
async def test_no_cookie_is_refused_everywhere(bare, caplog, path):
    with caplog.at_level(logging.WARNING):
        response = await bare.get(path)
    assert response.status == 403
    assert "no key" in caplog.text


async def test_wrong_cookie_is_refused(bare):
    bare.session.cookie_jar.update_cookies({COOKIE_NAME: "other"})
    assert (await bare.get("/")).status == 403


async def test_the_secret_is_never_logged(bare, caplog):
    with caplog.at_level(logging.DEBUG):
        await bare.get(f"/?key={KEY}")
        await bare.get("/?key=wrong-guess")
        await bare.get("/")
        ws = await connect(bare)
        await ws.close()
    ours = [r.getMessage() for r in caplog.records if r.name.startswith("jedimaster")]
    assert ours
    assert not any(KEY in m or "wrong-guess" in m for m in ours)


# ---- the host -------------------------------------------------------------


async def test_wrong_host_is_refused_even_with_the_key(client, caplog):
    with caplog.at_level(logging.WARNING):
        response = await client.get("/", headers={"Host": "attacker.example"})
    assert response.status == 403
    assert "wrong host" in caplog.text


async def test_host_with_the_wrong_port_is_refused(client):
    response = await client.get("/", headers={"Host": f"127.0.0.1:{client.port + 1}"})
    assert response.status == 403


@pytest.mark.parametrize("name", ["127.0.0.1", "localhost"])
async def test_both_local_names_are_accepted(client, name):
    response = await client.get("/", headers={"Host": f"{name}:{client.port}"})
    assert response.status == 200


# ---- headers and files ----------------------------------------------------


async def test_security_headers_on_every_response(client, bare):
    for response in (
        await client.get("/"),
        await client.get("/main.js"),
        await client.get("/missing"),
        await bare.get("/"),
        await client.get("/", headers={"Host": "attacker.example"}),
    ):
        headers = response.headers
        policy = headers["Content-Security-Policy"]
        port = response.url.port
        assert "default-src 'self'" in policy
        assert f"ws://127.0.0.1:{port}" in policy
        assert f"ws://localhost:{port}" in policy
        assert "frame-ancestors 'none'" in policy
        assert headers["X-Content-Type-Options"] == "nosniff"
        assert headers["Referrer-Policy"] == "no-referrer"
        assert headers["Cache-Control"] == "no-store"


async def test_a_request_no_route_answers_still_carries_the_headers(client):
    response = await client.post("/")
    assert response.status == 405
    assert response.headers["Content-Security-Policy"].startswith("default-src 'self'")
    assert response.headers["Referrer-Policy"] == "no-referrer"
    assert response.headers["Cache-Control"] == "no-store"


async def test_a_script_is_served(client):
    response = await client.get("/main.js")
    assert response.status == 200
    assert await response.text() == "export {};\n"


async def test_a_page_that_is_not_built_says_so(aiohttp_client, control, tmp_path):
    client = await aiohttp_client(make_app(control, KEY, tmp_path / "nothing"))
    await client.get(f"/?key={KEY}", allow_redirects=False)
    response = await client.get("/")
    assert response.status == 404
    assert "not built" in await response.text()
    assert (await client.get("/missing")).headers["X-Content-Type-Options"] == "nosniff"


async def test_only_the_page_folder_is_served(client, page_dir):
    (page_dir.parent / "secret.txt").write_text("no", encoding="utf-8")
    for path in ("/secret.txt", "/..%2fsecret.txt", "/../secret.txt"):
        response = await client.get(path)
        assert response.status in (403, 404), path
        assert "no" != (await response.text()).strip()


# ---- the socket -----------------------------------------------------------


async def test_status_arrives_on_connect(client):
    ws = await connect(client)
    assert await receive(ws) == {
        "event": "status",
        "data": {"launcher_version": LAUNCHER_VERSION, "install_found": True},
    }
    await ws.close()


async def test_socket_needs_the_cookie(bare):
    with pytest.raises(aiohttp.WSServerHandshakeError) as caught:
        await connect(bare)
    assert caught.value.status == 403


@pytest.mark.parametrize(
    "origin", ["http://attacker.example", "https://127.0.0.1", "null", "", None]
)
async def test_wrong_origin_is_refused_before_the_upgrade(client, caplog, origin):
    headers = {} if origin is None else {"Origin": origin}
    with (
        caplog.at_level(logging.WARNING),
        pytest.raises(aiohttp.WSServerHandshakeError) as caught,
    ):
        await client.ws_connect("/ws", headers=headers)
    assert caught.value.status == 403
    assert "wrong origin" in caplog.text


async def test_origin_with_another_port_is_refused(client):
    with pytest.raises(aiohttp.WSServerHandshakeError):
        await connect(client, Origin=f"http://127.0.0.1:{client.port + 1}")


async def test_every_command_over_the_socket(client):
    ws = await connect(client)
    await receive(ws)
    hello = await ask(ws, 1, "hello", {"page_version": "0.1.0"})
    assert hello["result"]["launcher_version"] == LAUNCHER_VERSION
    assert (await ask(ws, 2, "install.status"))["result"]["found"] is True
    menus = (await ask(ws, 3, "missions.list"))["result"]["menus"]
    assert menus[0]["entries"][0]["title"] == "First Flight"
    assert (await ask(ws, 4, "settings.get"))["result"]["settings"] == {
        "art_scaling": "whole_pixels"
    }
    await ws.close()


async def test_refusals_over_the_socket_keep_it_open(client, caplog):
    ws = await connect(client)
    await receive(ws)
    with caplog.at_level(logging.WARNING):
        await ws.send_str("not json")
        assert (await receive(ws))["error"]["code"] == "bad_message"
        reply = await ask(ws, 7, "shutdown")
        assert reply["error"]["code"] == "unknown_command" and reply["id"] == 7
        args = {"name": "art_scaling", "value": "stretched"}
        reply = await ask(ws, 8, "settings.set", args)
        assert reply["error"]["code"] == "bad_arguments"
        await ws.send_bytes(b"\x00\x01")
        assert (await receive(ws))["error"]["code"] == "bad_message"
    assert (await ask(ws, 9, "settings.get"))["ok"] is True
    assert "unknown_command" in caplog.text
    await ws.close()


async def test_oversize_message_closes_the_socket(client, caplog):
    ws = await connect(client)
    await receive(ws)
    with caplog.at_level(logging.WARNING):
        await ws.send_str("x" * (MAX_MESSAGE_BYTES + 1))
        message = await ws.receive(timeout=5)
    assert message.type in (aiohttp.WSMsgType.CLOSE, aiohttp.WSMsgType.CLOSED)
    assert ws.close_code == aiohttp.WSCloseCode.MESSAGE_TOO_BIG
    assert "socket stopped" in caplog.text


async def test_message_at_the_cap_is_answered(client):
    ws = await connect(client)
    await receive(ws)
    padding = "x" * (MAX_MESSAGE_BYTES - 200)
    await ws.send_str(request(1, "hello", {"page_version": padding}))
    assert (await receive(ws))["error"]["code"] == "bad_arguments"
    await ws.close()


async def test_a_setting_reaches_a_second_page_and_the_file(client, control):
    first = await connect(client)
    second = await connect(client)
    await receive(first)
    await receive(second)
    args = {"name": "art_scaling", "value": "engine_fit"}
    reply = await ask(first, 5, "settings.set", args)
    assert reply["result"] == {"settings": {"art_scaling": "engine_fit"}}
    changed = {
        "event": "settings.changed",
        "data": {"settings": {"art_scaling": "engine_fit"}},
    }
    assert await receive(first) == changed
    assert await receive(second) == changed
    assert (await ask(second, 1, "settings.get"))["result"]["settings"] == {
        "art_scaling": "engine_fit"
    }
    saved = json.loads(control.store.path.read_text(encoding="utf-8"))
    assert saved == {"art_scaling": "engine_fit"}
    await first.close()
    await second.close()


async def test_show_mission_reaches_every_open_page(client):
    first = await connect(client)
    second = await connect(client)
    await receive(first)
    await receive(second)
    args = {"mission_type": "training", "id": 1}
    reply = await ask(first, 6, "page.show_mission", args)
    assert reply == {"id": 6, "ok": True, "result": {"shown": True}}
    shown = {
        "event": "page.show_mission",
        "data": {"mission_type": "training", "id": 1, "title": "First Flight"},
    }
    assert await receive(first) == shown
    assert await receive(second) == shown
    await first.close()
    await second.close()


async def test_a_mission_that_is_not_listed_is_answered_and_pushed_to_nobody(client):
    first = await connect(client)
    second = await connect(client)
    await receive(first)
    await receive(second)
    args = {"mission_type": "training", "id": 99}
    reply = await ask(first, 6, "page.show_mission", args)
    assert reply["result"] == {"shown": False}
    reply = await ask(second, 1, "settings.get")
    assert reply["id"] == 1
    await first.close()
    await second.close()


async def test_the_broadcast_counts_the_open_pages_and_sends_in_order(client):
    first = await connect(client)
    await receive(first)
    pushes = [
        {
            "event": "settings.changed",
            "data": {"settings": {"art_scaling": "engine_fit"}},
        },
        {
            "event": "settings.changed",
            "data": {"settings": {"art_scaling": "whole_pixels"}},
        },
    ]
    assert await server.broadcast(client.app, pushes) == 1
    assert (await receive(first))["data"]["settings"]["art_scaling"] == "engine_fit"
    assert (await receive(first))["data"]["settings"]["art_scaling"] == "whole_pixels"
    await first.close()


async def test_a_closed_page_is_dropped_from_the_pushes(client):
    first = await connect(client)
    second = await connect(client)
    await receive(first)
    await receive(second)
    await second.close()
    for _ in range(100):
        if len(client.app[SOCKETS]) == 1:
            break
        await asyncio.sleep(0.02)
    assert len(client.app[SOCKETS]) == 1
    args = {"name": "art_scaling", "value": "sharp_bilinear"}
    reply = await ask(first, 2, "settings.set", args)
    assert reply["ok"] is True
    assert (await receive(first))["event"] == "settings.changed"
    await first.close()


async def test_connect_and_close_are_logged(client, caplog):
    with caplog.at_level(logging.INFO):
        ws = await connect(client)
        await ws.close()
        await client.get("/")
    assert "page connected" in caplog.text


async def test_the_secret_is_compared_in_constant_time(client, bare, monkeypatch):
    calls = []
    real = server.hmac.compare_digest

    def counting(a, b):
        calls.append((a, b))
        return real(a, b)

    monkeypatch.setattr(server.hmac, "compare_digest", counting)
    await bare.get(f"/?key={KEY}", allow_redirects=False)
    await client.get("/")
    assert len(calls) == 2


@pytest.mark.parametrize("open_browser", [False, True])
async def test_serve_binds_locally_prints_the_url_and_logs_no_secret(
    control, page_dir, capsys, caplog, monkeypatch, open_browser
):
    sites, opened = [], []
    real_site = server.web.TCPSite

    def recording_site(runner, host, port):
        sites.append((host, port))
        return real_site(runner, host, port)

    monkeypatch.setattr(server.web, "TCPSite", recording_site)
    monkeypatch.setattr(server.webbrowser, "open", opened.append)
    with caplog.at_level(logging.DEBUG):
        task = asyncio.create_task(serve(control, KEY, 0, page_dir, open_browser))
        url = ""
        for _ in range(100):
            url = capsys.readouterr().out.strip()
            if url:
                break
            await asyncio.sleep(0.05)
        assert url.startswith("http://127.0.0.1:") and url.endswith(f"/?key={KEY}")
        jar = aiohttp.CookieJar(unsafe=True)
        async with aiohttp.ClientSession(cookie_jar=jar) as session:
            async with session.get(url) as response:
                assert response.status == 200
                assert await response.text() == PAGE
        task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await task
    assert sites == [("127.0.0.1", 0)]
    assert opened == ([url] if open_browser else [])
    assert KEY not in caplog.text
    assert "aiohttp.access" not in caplog.text


async def test_serve_calls_on_start_once_with_the_bound_application(
    control, page_dir, monkeypatch, capsys
):
    monkeypatch.setattr(server.webbrowser, "open", lambda url: None)
    started: list[tuple[object, str]] = []

    def on_start(app):
        started.append((app, capsys.readouterr().out.strip()))

    task = asyncio.create_task(serve(control, KEY, 0, page_dir, False, on_start))
    for _ in range(100):
        if started:
            break
        await asyncio.sleep(0.05)
    assert len(started) == 1
    app, printed = started[0]
    assert app[server.CONTROL] is control
    assert app[SOCKETS] == set()
    assert printed.endswith(f"/?key={KEY}")
    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task
    assert len(started) == 1
