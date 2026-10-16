"""The matchmaker's HTTPS server: the directory's routes, for games on the internet.

Purpose:
    Serve the ``Directory`` to the games of a network battle over TLS, with
    the install's own certificate, on the one port the host opens on their
    router. It is the first part of the launcher that other machines reach,
    so it reads no more than the game's client ever sends and says nothing
    about itself.

Flow:
    ``make_app`` routes every request under ``/v1/apps/{game}/rooms`` to the
    directory, the same routes Aeron's directory client calls, and turns
    each refusal into the client's ``{"error": name}``; ``serve`` binds
    ``host`` (every address of the computer by default) on ``port`` with
    the TLS context and runs until cancelled.

Invariants:
    - A body over 8,192 bytes, the most the game's client ever sends, is
      refused as body_too_large (413) before it is parsed.
    - Every refusal is JSON the client reads, in its error names.
    - No access log (its lines would carry game ids) and no web framework
      name or version in the Server header.
    - A refusal of a request under an issued game is a WARNING; one under
      any other id, what a stranger or a scanner gets, is DEBUG only.

Call:
    ``await serve(directory, tls_context(certificate), 8781)``
"""

from __future__ import annotations

import asyncio
import json
import logging
import ssl
from collections.abc import Callable

from aiohttp import web

from .directory import Directory
from .directory import DirectoryError
from .directory import read_answer
from .directory import shown

logger = logging.getLogger(__name__)

MAX_BODY_BYTES = 8192
SERVER_NAME = "matchmaker"
DIRECTORY = web.AppKey("directory", Directory)


def bearer(request: web.Request) -> str:
    """Return the request's bearer token, or "" when it carries none."""
    header = request.headers.get("Authorization", "")
    return header[7:] if header.startswith("Bearer ") else ""


async def read_body(request: web.Request) -> dict[str, object]:
    """Return the request's JSON object; refuse an oversized or other body."""
    try:
        raw = await request.read()
    except web.HTTPRequestEntityTooLarge as err:
        raise DirectoryError(413, "body_too_large") from err
    try:
        body = json.loads(raw or b"null")
    except (ValueError, UnicodeDecodeError) as err:
        raise DirectoryError(400, "invalid_request") from err
    if not isinstance(body, dict):
        raise DirectoryError(400, "invalid_request")
    return body


def reply(status: int, payload: dict[str, object] | None) -> web.Response:
    """Return ``payload`` as compact JSON with ``status``, or an empty body."""
    if payload is None:
        return web.Response(status=status)
    data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    return web.Response(status=status, body=data, content_type="application/json")


async def route(
    directory: Directory, request: web.Request, game: str, rest: list[str]
) -> web.Response:
    """Answer one request under ``game``'s rooms; ``rest`` is the path after it."""
    method = request.method
    token = bearer(request)
    if method == "GET" and not rest:
        return reply(200, directory.list_rooms(game))
    if method == "PUT" and len(rest) == 1:
        return reply(
            *directory.put_room(game, rest[0], token, await read_body(request))
        )
    if method == "DELETE" and len(rest) == 1:
        directory.delete_room(game, rest[0], token)
        return reply(204, None)
    if method == "GET" and rest[1:] == ["connections"]:
        cursor = request.query.get("cursor")
        return reply(
            200, await directory.poll_connections(game, rest[0], token, cursor)
        )
    if len(rest) == 3 and rest[1] == "connections":
        room_id, connection_id = rest[0], rest[2]
        if method == "PUT":
            body = await read_body(request)
            return reply(
                *directory.put_connection(game, room_id, connection_id, token, body)
            )
        if method == "GET":
            return reply(
                200,
                await directory.poll_connection(game, room_id, connection_id, token),
            )
        if method == "DELETE":
            directory.delete_connection(game, room_id, connection_id, token)
            return reply(204, None)
        raise DirectoryError(405, "method_not_allowed")
    if method == "PUT" and len(rest) == 4 and rest[1] == "connections":
        room_id, connection_id, leg = rest[0], rest[2], rest[3]
        body = await read_body(request)
        if leg == "offer":
            sdp = body.get("sdp")
            if len(body) != 1 or not isinstance(sdp, str) or not sdp:
                raise DirectoryError(400, "invalid_request")
            return reply(
                200, directory.put_offer(game, room_id, connection_id, token, sdp)
            )
        if leg == "answer":
            answer = read_answer(body)
            directory.put_answer(game, room_id, connection_id, token, answer)
            return reply(204, None)
        raise DirectoryError(404, "not_found")
    raise DirectoryError(405, "method_not_allowed")


async def handle(request: web.Request) -> web.Response:
    """Answer any request: the directory's routes, a JSON refusal otherwise."""
    directory = request.app[DIRECTORY]
    parts = request.path.strip("/").split("/")
    game = parts[2] if len(parts) > 2 else ""
    try:
        if len(parts) < 4 or parts[:2] != ["v1", "apps"] or parts[3] != "rooms":
            raise DirectoryError(404, "not_found")
        return await route(directory, request, game, parts[4:])
    except DirectoryError as err:
        path = "/".join(["", *parts[:2], shown(game), *parts[3:]])
        level = logging.WARNING if game in directory.games else logging.DEBUG
        logger.log(
            level, "refused %s %s: %d %s", request.method, path, err.status, err.name
        )
        return reply(err.status, {"error": err.name})


async def name_server(request: web.Request, response: web.StreamResponse) -> None:
    """Put the plain server name in place of the web framework's own."""
    response.headers["Server"] = SERVER_NAME


def make_app(directory: Directory) -> web.Application:
    """Return the web application serving ``directory``."""
    app = web.Application(client_max_size=MAX_BODY_BYTES)
    app[DIRECTORY] = directory
    app.router.add_route("*", "/{tail:.*}", handle)
    app.on_response_prepare.append(name_server)
    return app


async def serve(
    directory: Directory,
    context: ssl.SSLContext,
    port: int,
    host: str | None = None,
    on_start: Callable[[int], None] | None = None,
) -> None:
    """Serve ``directory`` over TLS on ``host``:``port`` until cancelled.

    ``host`` None binds every address of the computer; ``port`` 0 takes a
    free one, best with one ``host``. Calls ``on_start`` once with the bound
    port. Raises ``OSError`` when the port cannot be bound.
    """
    runner = web.AppRunner(make_app(directory), access_log=None)
    await runner.setup()
    try:
        await web.TCPSite(runner, host, port, ssl_context=context).start()
        bound = runner.addresses[0][1]
        logger.info("matchmaker serving on port %d", bound)
        if on_start is not None:
            on_start(bound)
        await asyncio.Event().wait()
    finally:
        await runner.cleanup()
