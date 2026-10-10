"""The local web server: the page, its socket, and the guards around both.

Purpose:
    Serve the built page and one WebSocket (``/ws``) on 127.0.0.1, and let
    only the player's own browser use either: a request must carry the
    run's secret, name this server in its ``Host`` header, and (for the
    socket) come from the page's own origin.

Flow:
    ``make_app`` builds the application around a ``Control``. Three
    middlewares run on every request: ``add_headers`` (security headers on
    every response, refusals included), ``check_host`` (DNS-rebinding
    guard), ``check_key`` (the secret: ``GET /?key=K`` sets a cookie and
    redirects to ``/``; every other request needs the cookie). ``socket``
    checks the origin, then reads one JSON message at a time, hands it to
    ``Control.handle``, sends the reply and then the pushes to every open
    socket. ``serve`` binds, prints the page's URL and runs until stopped.

Invariants:
    - The server binds to 127.0.0.1 only.
    - The secret is compared in constant time and is never logged; the
      access log is off because it would print the secret's URL.
    - Every refusal is logged at WARNING, without the secret or the cookie.
    - A message over 64 KiB closes the socket.
    - This is the only module that imports the web framework.

Call:
    ``asyncio.run(serve(control, key, 8780, Path("page/dist"), open_browser=True))``
"""

from __future__ import annotations

import asyncio
import hmac
import json
import logging
import webbrowser
from collections.abc import Awaitable
from collections.abc import Callable
from pathlib import Path

from aiohttp import web
from aiohttp import WSMsgType

from .control import Control
from .control import Outcome
from .protocol import MAX_MESSAGE_BYTES

logger = logging.getLogger(__name__)

HOST = "127.0.0.1"
HOSTNAMES = ("127.0.0.1", "localhost")
COOKIE_NAME = "jedimaster_key"
CONTROL = web.AppKey("control", Control)
KEY = web.AppKey("key", str)
PAGE_DIR = web.AppKey("page_dir", Path)
SOCKETS = web.AppKey("sockets", set)
RUNNER_OPTIONS = {"access_log": None}
"""The access log is off: it would print the secret's URL."""

Handler = Callable[[web.Request], Awaitable[web.StreamResponse]]


def _local_port(request: web.Request) -> int:
    transport = request.transport
    if transport is None:
        return 0
    return int(transport.get_extra_info("sockname")[1])


def _allowed_hosts(request: web.Request) -> tuple[str, ...]:
    port = _local_port(request)
    return tuple(f"{name}:{port}" for name in HOSTNAMES)


def _policy(request: web.Request) -> str:
    port = _local_port(request)
    sockets = " ".join(f"ws://{name}:{port}" for name in HOSTNAMES)
    return f"default-src 'self'; connect-src 'self' {sockets}; frame-ancestors 'none'"


def _refuse(request: web.Request, status: int, reason: str) -> web.Response:
    logger.warning("refused %s %s: %s", request.method, request.path, reason)
    return web.Response(status=status, text=f"refused: {reason}\n")


def _same(given: str, wanted: str) -> bool:
    return hmac.compare_digest(given.encode("utf-8"), wanted.encode("utf-8"))


def _secure(response: web.StreamResponse, request: web.Request) -> None:
    response.headers["Content-Security-Policy"] = _policy(request)
    response.headers["X-Content-Type-Options"] = "nosniff"
    response.headers["Referrer-Policy"] = "no-referrer"
    response.headers["Cache-Control"] = "no-store"


@web.middleware
async def add_headers(request: web.Request, handler: Handler) -> web.StreamResponse:
    """Put the security headers on the response, whatever it is."""
    try:
        response = await handler(request)
    except web.HTTPException as exc:
        _secure(exc, request)
        raise
    _secure(response, request)
    return response


@web.middleware
async def check_host(request: web.Request, handler: Handler) -> web.StreamResponse:
    """Refuse a request whose Host header does not name this server."""
    if request.headers.get("Host", "") not in _allowed_hosts(request):
        return _refuse(request, 403, "wrong host")
    return await handler(request)


@web.middleware
async def check_key(request: web.Request, handler: Handler) -> web.StreamResponse:
    """Let through only the request that carries the run's secret."""
    secret = request.app[KEY]
    if request.path == "/" and "key" in request.query:
        if not _same(request.query["key"], secret):
            return _refuse(request, 403, "wrong key")
        response = web.Response(status=302, headers={"Location": "/"})
        response.set_cookie(
            COOKIE_NAME, secret, httponly=True, samesite="Strict", path="/"
        )
        logger.info("page opened with the key")
        return response
    if not _same(request.cookies.get(COOKIE_NAME, ""), secret):
        return _refuse(request, 403, "no key")
    return await handler(request)


async def index(request: web.Request) -> web.StreamResponse:
    """Return the page's ``index.html``, or 404 when the page is not built."""
    page = request.app[PAGE_DIR] / "index.html"
    if not page.is_file():
        return _refuse(request, 404, "the page is not built")
    return web.FileResponse(page)


async def _send(ws: web.WebSocketResponse, message: dict) -> None:
    try:
        await ws.send_str(json.dumps(message))
    except (ConnectionError, RuntimeError) as exc:
        logger.warning("cannot send to a page: %s", exc)


async def _broadcast(app: web.Application, outcome: Outcome) -> None:
    for push in outcome.pushes:
        for other in list(app[SOCKETS]):
            await _send(other, push)


async def socket(request: web.Request) -> web.StreamResponse:
    """Run one page's socket until it closes; return the closed socket.

    Refuses (403, before the upgrade) an ``Origin`` other than
    ``http://`` plus this request's own host.
    """
    origin = request.headers.get("Origin", "")
    if origin != f"http://{request.headers.get('Host', '')}":
        return _refuse(request, 403, "wrong origin")
    ws = web.WebSocketResponse(max_msg_size=MAX_MESSAGE_BYTES)
    await ws.prepare(request)
    control = request.app[CONTROL]
    request.app[SOCKETS].add(ws)
    logger.info("page connected")
    try:
        await _send(ws, control.status_push())
        async for msg in ws:
            if msg.type == WSMsgType.TEXT:
                outcome = control.handle(msg.data)
            elif msg.type == WSMsgType.BINARY:
                logger.warning("refused a binary message")
                outcome = control.handle("")
            else:
                logger.warning("socket stopped: %s", msg.type.name)
                break
            await _send(ws, outcome.reply)
            await _broadcast(request.app, outcome)
    finally:
        request.app[SOCKETS].discard(ws)
        logger.info("page closed")
    return ws


async def _close_sockets(app: web.Application) -> None:
    for ws in list(app[SOCKETS]):
        await ws.close()


def make_app(control: Control, key: str, page_dir: Path) -> web.Application:
    """Return the application serving ``page_dir`` and ``/ws`` for ``control``.

    Does not bind a port. A ``page_dir`` that does not exist serves nothing
    (``/`` answers 404). Every request still passes the three middlewares.
    """
    app = web.Application(middlewares=[add_headers, check_host, check_key])
    app[CONTROL] = control
    app[KEY] = key
    app[PAGE_DIR] = page_dir
    app[SOCKETS] = set()
    app.router.add_get("/", index)
    app.router.add_get("/ws", socket)
    if page_dir.is_dir():
        app.router.add_static("/", page_dir)
    app.on_shutdown.append(_close_sockets)
    return app


async def serve(
    control: Control, key: str, port: int, page_dir: Path, open_browser: bool
) -> None:
    """Bind 127.0.0.1:``port`` (0 takes a free one), print the URL, run on.

    Prints ``http://127.0.0.1:PORT/?key=SECRET`` and opens it in the browser
    unless ``open_browser`` is False. Runs until cancelled, then closes the
    sockets and the server. Raises ``OSError`` when the port cannot be
    bound.
    """
    runner = web.AppRunner(make_app(control, key, page_dir), **RUNNER_OPTIONS)
    await runner.setup()
    try:
        await web.TCPSite(runner, HOST, port).start()
        bound = runner.addresses[0][1]
        url = f"http://{HOST}:{bound}/?key={key}"
        logger.info("serving on %s:%d", HOST, bound)
        print(url, flush=True)
        if open_browser:
            webbrowser.open(url)
        await asyncio.Event().wait()
    finally:
        await runner.cleanup()
