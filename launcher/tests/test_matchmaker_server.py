"""Fences for the matchmaker's server: the client's walk, its refusals, the limits.

The walk is the order Aeron's directory client takes, as the companion's
stand-in lobby's fences walk it: list, host a room, poll its connections,
open a join, offer, answer, read the answer, delete. Each reply is checked
for the exact field set the client accepts. The server runs on the test
client's plain HTTP; TLS is fenced in ``test_matchmaker_certificate`` and
``test_matchmaker_cli``.
"""

from __future__ import annotations

import asyncio
import json
import logging
from dataclasses import dataclass

import pytest

from jedimaster.matchmaker.directory import Directory
from jedimaster.matchmaker.server import make_app
from jedimaster.matchmaker.server import MAX_BODY_BYTES

STRANGER = "11111111-2222-4333-8444-555555555555"
ROOM = "aaaaaaaa-0000-4000-8000-000000000001"
CONNECTION = "cccccccc-0000-4000-8000-000000000002"
HOST_TOKEN = "host-token"
JOIN_TOKEN = "join-token"
ROOM_BODY = {
    "revision": 1,
    "protocol": 2,
    "game_version": "7",
    "name": "Rig game",
    "players": 1,
    "max_players": 8,
    "password_required": False,
    "joinable": True,
    "state": "lobby",
    "mission": None,
    "roster": [{"name": "Host", "rating": 1}],
}
ROOM_FIELDS = {"room_id", "expires_at", *ROOM_BODY}
JOIN_FIELDS = {"connection_id", "expires_at", "ice", "state", "answer"}
HOST_FIELDS = JOIN_FIELDS | {"offer"}


@dataclass
class Caller:
    """The test client, the directory behind it and its one issued game."""

    client: object
    directory: Directory
    game: str

    async def __call__(
        self,
        method: str,
        path: str,
        token: str = "",
        body: object = None,
        game: str | None = None,
        raw: bytes | None = None,
        prefix: str | None = None,
    ) -> tuple[int, object]:
        """Send one request under a game's rooms; return (status, JSON or None)."""
        headers = {"Accept": "application/json"}
        if token:
            headers["Authorization"] = f"Bearer {token}"
        data = raw if raw is not None else None if body is None else json.dumps(body)
        url = prefix or f"/v1/apps/{game or self.game}/rooms"
        async with self.client.request(
            method, url + path, data=data, headers=headers
        ) as response:
            text = await response.read()
            return response.status, (json.loads(text) if text else None)


@pytest.fixture
async def call(aiohttp_client) -> Caller:
    directory = Directory("localhost", long_poll_seconds=0.3)
    game = directory.issue_game()
    return Caller(await aiohttp_client(make_app(directory)), directory, game)


async def test_host_and_join_walk_the_client_protocol(call: Caller) -> None:
    assert await call("GET", "") == (200, {"rooms": []})

    status, room = await call("PUT", f"/{ROOM}", HOST_TOKEN, ROOM_BODY)
    assert status == 201
    assert set(room) == ROOM_FIELDS
    assert {key: room[key] for key in ROOM_BODY} == ROOM_BODY

    status, listing = await call("GET", "")
    assert status == 200
    assert [item["room_id"] for item in listing["rooms"]] == [ROOM]

    status, polled = await call("GET", f"/{ROOM}/connections", HOST_TOKEN)
    assert status == 200
    assert polled["connections"] == []
    cursor = polled["cursor"]
    assert cursor

    status, joined = await call(
        "PUT",
        f"/{ROOM}/connections/{CONNECTION}",
        JOIN_TOKEN,
        {"protocol": 2, "game_version": "7"},
    )
    assert status == 201
    assert set(joined) == JOIN_FIELDS
    assert joined["state"] == "waiting_offer"
    assert joined["answer"] is None

    status, polled = await call(
        "GET", f"/{ROOM}/connections?cursor={cursor}", HOST_TOKEN
    )
    assert polled["cursor"] != cursor
    (signal,) = polled["connections"]
    assert set(signal) == HOST_FIELDS
    assert signal["offer"] is None

    status, offered = await call(
        "PUT", f"/{ROOM}/connections/{CONNECTION}/offer", JOIN_TOKEN, {"sdp": "o"}
    )
    assert status == 200
    assert offered["state"] == "waiting_answer"

    status, polled = await call("GET", f"/{ROOM}/connections", HOST_TOKEN)
    assert polled["connections"][0]["offer"] == {"sdp": "o"}

    assert await call(
        "PUT", f"/{ROOM}/connections/{CONNECTION}/answer", HOST_TOKEN, {"sdp": "a"}
    ) == (204, None)

    status, answered = await call(
        "GET", f"/{ROOM}/connections/{CONNECTION}", JOIN_TOKEN
    )
    assert status == 200
    assert answered["state"] == "answered"
    assert answered["answer"] == {"sdp": "a"}
    assert answered["ice"] == joined["ice"]
    assert answered["expires_at"] == joined["expires_at"]

    assert await call("DELETE", f"/{ROOM}/connections/{CONNECTION}", JOIN_TOKEN) == (
        204,
        None,
    )
    assert await call("DELETE", f"/{ROOM}", HOST_TOKEN) == (204, None)
    assert await call("GET", "") == (200, {"rooms": []})


async def test_a_current_cursor_waits_then_returns_unchanged(call: Caller) -> None:
    await call("PUT", f"/{ROOM}", HOST_TOKEN, ROOM_BODY)
    _, first = await call("GET", f"/{ROOM}/connections", HOST_TOKEN)
    loop = asyncio.get_running_loop()
    started = loop.time()
    _, again = await call(
        "GET", f"/{ROOM}/connections?cursor={first['cursor']}", HOST_TOKEN
    )
    assert loop.time() - started >= call.directory.long_poll_seconds
    assert again == first


async def test_refusals_carry_the_client_error_names(call: Caller) -> None:
    await call("PUT", f"/{ROOM}", HOST_TOKEN, ROOM_BODY)
    assert await call("PUT", f"/{ROOM}", "other", ROOM_BODY) == (
        401,
        {"error": "unauthorized"},
    )
    assert await call(
        "PUT",
        f"/{ROOM}/connections/{CONNECTION}",
        JOIN_TOKEN,
        {"protocol": 2, "game_version": "8"},
    ) == (409, {"error": "incompatible"})
    closed = {**ROOM_BODY, "joinable": False}
    await call("PUT", f"/{ROOM}", HOST_TOKEN, closed)
    assert await call(
        "PUT",
        f"/{ROOM}/connections/{CONNECTION}",
        JOIN_TOKEN,
        {"protocol": 2, "game_version": "7"},
    ) == (409, {"error": "not_joinable"})
    assert await call(
        "GET", "/bbbbbbbb-0000-4000-8000-000000000003/connections", "x"
    ) == (404, {"error": "not_found"})


async def test_a_refused_join_reads_as_rejected(call: Caller) -> None:
    await call("PUT", f"/{ROOM}", HOST_TOKEN, ROOM_BODY)
    path = f"/{ROOM}/connections/{CONNECTION}"
    await call("PUT", path, JOIN_TOKEN, {"protocol": 2, "game_version": "7"})
    await call("PUT", f"{path}/offer", JOIN_TOKEN, {"sdp": "o"})
    assert await call("PUT", f"{path}/answer", HOST_TOKEN, {"error": "full"}) == (
        204,
        None,
    )
    _, signal = await call("GET", path, JOIN_TOKEN)
    assert (signal["state"], signal["answer"]) == ("rejected", {"error": "full"})


async def test_a_stranger_lists_nothing_and_is_refused_the_rest(call: Caller) -> None:
    await call("PUT", f"/{ROOM}", HOST_TOKEN, ROOM_BODY)
    assert await call("GET", "", game=STRANGER) == (200, {"rooms": []})
    assert await call("PUT", f"/{ROOM}", HOST_TOKEN, ROOM_BODY, game=STRANGER) == (
        404,
        {"error": "not_found"},
    )
    assert await call("GET", f"/{ROOM}/connections", HOST_TOKEN, game=STRANGER) == (
        404,
        {"error": "not_found"},
    )


async def test_a_body_over_the_clients_largest_is_refused(call: Caller) -> None:
    assert MAX_BODY_BYTES == 8192  # the game client's request buffer, DP_HTTP_BODY
    sdp = "o" * (MAX_BODY_BYTES - len('{"sdp":""}'))
    await call("PUT", f"/{ROOM}", HOST_TOKEN, ROOM_BODY)
    path = f"/{ROOM}/connections/{CONNECTION}"
    await call("PUT", path, JOIN_TOKEN, {"protocol": 2, "game_version": "7"})
    exact = json.dumps({"sdp": sdp}, separators=(",", ":")).encode()
    assert len(exact) == MAX_BODY_BYTES
    status, offered = await call("PUT", f"{path}/offer", JOIN_TOKEN, raw=exact)
    assert (status, offered["state"]) == (200, "waiting_answer")
    over = json.dumps({"sdp": sdp + "o"}, separators=(",", ":")).encode()
    assert await call("PUT", f"{path}/offer", JOIN_TOKEN, raw=over) == (
        413,
        {"error": "body_too_large"},
    )


async def test_a_body_that_is_not_one_json_object_is_refused(call: Caller) -> None:
    for raw in (b"{", b"[]", b"\xff"):
        assert await call("PUT", f"/{ROOM}", HOST_TOKEN, raw=raw) == (
            400,
            {"error": "invalid_request"},
        )


async def test_other_paths_and_methods_answer_json_refusals(call: Caller) -> None:
    assert await call("GET", "", prefix="/") == (404, {"error": "not_found"})
    assert await call("GET", "", prefix=f"/v1/other/{call.game}/rooms") == (
        404,
        {"error": "not_found"},
    )
    assert await call("GET", "", prefix=f"/v1/apps/{call.game}/lobbies") == (
        404,
        {"error": "not_found"},
    )
    assert await call("POST", "") == (405, {"error": "method_not_allowed"})
    assert await call("PATCH", f"/{ROOM}/connections/{CONNECTION}") == (
        405,
        {"error": "method_not_allowed"},
    )
    assert await call("PUT", f"/{ROOM}/connections/{CONNECTION}/other", body={}) == (
        404,
        {"error": "not_found"},
    )


async def test_the_server_does_not_name_its_framework(call: Caller) -> None:
    for path in ("", "/missing/connections"):
        async with call.client.get(f"/v1/apps/{call.game}/rooms{path}") as response:
            assert response.headers["Server"] == "matchmaker"


async def test_refusals_warn_only_under_an_issued_game(
    call: Caller, caplog: pytest.LogCaptureFixture
) -> None:
    caplog.set_level(logging.DEBUG, logger="jedimaster.matchmaker")
    await call("PUT", f"/{ROOM}", "x", ROOM_BODY, game=STRANGER)
    assert not [r for r in caplog.records if r.levelno >= logging.WARNING]
    await call("GET", f"/{ROOM}/connections", HOST_TOKEN)
    (warning,) = [r for r in caplog.records if r.levelno >= logging.WARNING]
    assert call.game[:8] in warning.getMessage()
    assert call.game not in caplog.text
