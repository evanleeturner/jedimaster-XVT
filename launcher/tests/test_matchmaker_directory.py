"""Fences for the matchmaker's directory: issued games only, the leases, the waits.

The directory is driven directly, its clock a counter the test moves, so a
lease is checked to the second; the waits run on the real event loop with
short long polls.
"""

from __future__ import annotations

import asyncio
import logging
import re

import pytest

from jedimaster.matchmaker.directory import CONNECTION_LEASE_SECONDS
from jedimaster.matchmaker.directory import Directory
from jedimaster.matchmaker.directory import DirectoryError
from jedimaster.matchmaker.directory import MAX_PLAYERS
from jedimaster.matchmaker.directory import MAX_ROOMS_PER_GAME
from jedimaster.matchmaker.directory import read_answer
from jedimaster.matchmaker.directory import ROOM_LEASE_SECONDS

STRANGER = "11111111-2222-4333-8444-555555555555"
ROOM = "aaaaaaaa-0000-4000-8000-000000000001"
CONNECTION = "cccccccc-0000-4000-8000-000000000002"
HOST_TOKEN = "host-token-7f3a"
JOIN_TOKEN = "join-token-91bd"
ROOM_BODY = {
    "revision": 1,
    "protocol": 2,
    "game_version": "103",
    "name": "Door game",
    "players": 1,
    "max_players": 8,
    "password_required": False,
    "joinable": True,
    "state": "lobby",
    "mission": None,
    "roster": [{"name": "Host", "rating": 1}],
}
JOIN_BODY = {"protocol": 2, "game_version": "103"}
GUID_TEXT = r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"


class Clock:
    """A wall clock that moves only when the test says."""

    def __init__(self) -> None:
        self.now = 1_800_000_000.0

    def __call__(self) -> float:
        return self.now


@pytest.fixture
def clock() -> Clock:
    return Clock()


@pytest.fixture
def directory(clock: Clock) -> Directory:
    return Directory("localhost", clock, long_poll_seconds=0.2)


def refusal(call, *args) -> tuple[int, str]:
    """Return the (status, name) ``call`` refuses with; fail when it does not."""
    with pytest.raises(DirectoryError) as caught:
        call(*args)
    return caught.value.status, caught.value.name


def test_a_game_id_is_fresh_lowercase_guid_text(directory: Directory) -> None:
    games = {directory.issue_game() for _ in range(64)}
    assert len(games) == 64
    for game in games:
        assert re.fullmatch(GUID_TEXT, game)


def seed(directory: Directory) -> str:
    """Issue a game holding ROOM and its one connection; return the game."""
    game = directory.issue_game()
    directory.put_room(game, ROOM, HOST_TOKEN, ROOM_BODY)
    directory.put_connection(game, ROOM, CONNECTION, JOIN_TOKEN, JOIN_BODY)
    return game


def test_a_game_not_issued_lists_empty_and_refuses_the_rest(
    directory: Directory,
) -> None:
    game = seed(directory)
    assert directory.list_rooms(STRANGER) == {"rooms": []}
    calls = [
        (directory.put_room, ROOM, HOST_TOKEN, ROOM_BODY),
        (directory.put_connection, ROOM, CONNECTION, JOIN_TOKEN, JOIN_BODY),
        (directory.put_offer, ROOM, CONNECTION, JOIN_TOKEN, "o"),
        (directory.put_answer, ROOM, CONNECTION, HOST_TOKEN, {"sdp": "a"}),
        (directory.delete_connection, ROOM, CONNECTION, JOIN_TOKEN),
        (directory.delete_room, ROOM, HOST_TOKEN),
    ]
    for call, *args in calls:
        assert refusal(call, STRANGER, *args) == (404, "not_found")
    (room,) = directory.list_rooms(game)["rooms"]
    assert room["room_id"] == ROOM
    assert directory.games[game][ROOM].connections[CONNECTION].offer is None


async def test_polls_under_a_game_not_issued_are_refused_at_once(
    directory: Directory,
) -> None:
    seed(directory)
    for poll in (
        directory.poll_connections(STRANGER, ROOM, HOST_TOKEN, None),
        directory.poll_connection(STRANGER, ROOM, CONNECTION, JOIN_TOKEN),
    ):
        with pytest.raises(DirectoryError) as caught:
            await asyncio.wait_for(poll, 0.1)
        assert caught.value.name == "not_found"


def test_rooms_of_one_game_are_not_listed_under_another(directory: Directory) -> None:
    first, second = directory.issue_game(), directory.issue_game()
    directory.put_room(first, ROOM, HOST_TOKEN, ROOM_BODY)
    assert [room["room_id"] for room in directory.list_rooms(first)["rooms"]] == [ROOM]
    assert directory.list_rooms(second) == {"rooms": []}


def test_a_retired_game_loses_its_rooms(directory: Directory) -> None:
    game = directory.issue_game()
    directory.put_room(game, ROOM, HOST_TOKEN, ROOM_BODY)
    directory.retire_game(game)
    assert directory.list_rooms(game) == {"rooms": []}
    assert refusal(directory.put_room, game, ROOM, HOST_TOKEN, ROOM_BODY) == (
        404,
        "not_found",
    )
    directory.retire_game(game)


def test_a_room_lapses_a_lease_after_its_last_put(
    directory: Directory, clock: Clock
) -> None:
    game = directory.issue_game()

    def listed() -> list[str]:
        return [room["room_id"] for room in directory.list_rooms(game)["rooms"]]

    assert directory.put_room(game, ROOM, HOST_TOKEN, ROOM_BODY)[0] == 201
    assert directory.put_room(game, "unrenewed", HOST_TOKEN, ROOM_BODY)[0] == 201
    clock.now += ROOM_LEASE_SECONDS - 1
    assert directory.put_room(game, ROOM, HOST_TOKEN, ROOM_BODY)[0] == 200
    assert listed() == [ROOM, "unrenewed"]
    clock.now += 1
    assert listed() == [ROOM]
    clock.now += ROOM_LEASE_SECONDS - 2
    assert listed() == [ROOM]
    clock.now += 1
    assert listed() == []
    assert refusal(
        directory.put_connection, game, ROOM, CONNECTION, JOIN_TOKEN, JOIN_BODY
    ) == (404, "not_found")
    assert directory.put_room(game, ROOM, "a later host", ROOM_BODY)[0] == 201


def test_the_rooms_of_a_game_are_capped_and_a_lapsed_one_frees_a_place(
    directory: Directory, clock: Clock
) -> None:
    game = directory.issue_game()
    names = [f"room-{number}" for number in range(MAX_ROOMS_PER_GAME)]
    for name in reversed(names):
        directory.put_room(game, name, HOST_TOKEN, ROOM_BODY)
    listed = [room["room_id"] for room in directory.list_rooms(game)["rooms"]]
    assert listed == names
    assert refusal(directory.put_room, game, "one more", HOST_TOKEN, ROOM_BODY) == (
        503,
        "capacity",
    )
    clock.now += ROOM_LEASE_SECONDS
    assert directory.put_room(game, "one more", HOST_TOKEN, ROOM_BODY)[0] == 201


def test_a_full_room_frees_a_seat_when_a_connection_lapses(
    directory: Directory, clock: Clock
) -> None:
    game = directory.issue_game()
    directory.put_room(game, ROOM, HOST_TOKEN, ROOM_BODY)
    for number in range(MAX_PLAYERS):
        directory.put_connection(game, ROOM, f"join-{number}", JOIN_TOKEN, JOIN_BODY)
    assert refusal(
        directory.put_connection, game, ROOM, "late", JOIN_TOKEN, JOIN_BODY
    ) == (409, "full")
    for _ in range(3):
        clock.now += CONNECTION_LEASE_SECONDS / 3
        assert directory.put_room(game, ROOM, HOST_TOKEN, ROOM_BODY)[0] == 200
    assert refusal(directory.put_offer, game, ROOM, "join-0", JOIN_TOKEN, "o") == (
        404,
        "not_found",
    )
    status, signal = directory.put_connection(game, ROOM, "late", JOIN_TOKEN, JOIN_BODY)
    assert (status, signal["state"]) == (201, "waiting_offer")


def test_the_joiner_needs_the_rooms_protocol_version_and_joinable(
    directory: Directory,
) -> None:
    game = directory.issue_game()
    assert refusal(
        directory.put_room, game, ROOM, HOST_TOKEN, {**ROOM_BODY, "protocol": 1}
    ) == (409, "incompatible")
    directory.put_room(game, ROOM, HOST_TOKEN, ROOM_BODY)
    for body in ({**JOIN_BODY, "protocol": 3}, {**JOIN_BODY, "game_version": "102"}):
        assert refusal(
            directory.put_connection, game, ROOM, CONNECTION, JOIN_TOKEN, body
        ) == (409, "incompatible")
    directory.put_room(game, ROOM, HOST_TOKEN, {**ROOM_BODY, "joinable": False})
    assert refusal(
        directory.put_connection, game, ROOM, CONNECTION, JOIN_TOKEN, JOIN_BODY
    ) == (409, "not_joinable")


def test_only_the_tokens_that_made_a_thing_change_it(directory: Directory) -> None:
    game = directory.issue_game()
    unauthorized = (401, "unauthorized")
    directory.put_room(game, ROOM, HOST_TOKEN, ROOM_BODY)
    assert refusal(directory.put_room, game, ROOM, "other", ROOM_BODY) == unauthorized
    assert refusal(directory.delete_room, game, ROOM, "other") == unauthorized
    directory.put_connection(game, ROOM, CONNECTION, JOIN_TOKEN, JOIN_BODY)
    assert (
        refusal(directory.put_connection, game, ROOM, CONNECTION, "other", JOIN_BODY)
        == unauthorized
    )
    assert (
        refusal(directory.put_offer, game, ROOM, CONNECTION, HOST_TOKEN, "o")
        == unauthorized
    )
    assert (
        refusal(directory.put_answer, game, ROOM, CONNECTION, JOIN_TOKEN, {"sdp": "a"})
        == unauthorized
    )
    assert (
        refusal(directory.delete_connection, game, ROOM, CONNECTION, "other")
        == unauthorized
    )
    directory.delete_connection(game, ROOM, CONNECTION, HOST_TOKEN)
    directory.delete_room(game, ROOM, HOST_TOKEN)
    assert directory.list_rooms(game) == {"rooms": []}


def test_an_offer_and_an_answer_are_kept_once(directory: Directory) -> None:
    game = directory.issue_game()
    directory.put_room(game, ROOM, HOST_TOKEN, ROOM_BODY)
    directory.put_connection(game, ROOM, CONNECTION, JOIN_TOKEN, JOIN_BODY)
    directory.put_offer(game, ROOM, CONNECTION, JOIN_TOKEN, "o")
    assert directory.put_offer(game, ROOM, CONNECTION, JOIN_TOKEN, "o")["state"] == (
        "waiting_answer"
    )
    assert refusal(directory.put_offer, game, ROOM, CONNECTION, JOIN_TOKEN, "p") == (
        409,
        "conflict",
    )
    directory.put_answer(game, ROOM, CONNECTION, HOST_TOKEN, {"sdp": "a"})
    directory.put_answer(game, ROOM, CONNECTION, HOST_TOKEN, {"sdp": "a"})
    assert refusal(
        directory.put_answer, game, ROOM, CONNECTION, HOST_TOKEN, {"error": "full"}
    ) == (409, "conflict")


def test_the_joiners_ice_block_and_expiry_never_change(
    directory: Directory, clock: Clock
) -> None:
    game = directory.issue_game()
    directory.put_room(game, ROOM, HOST_TOKEN, ROOM_BODY)
    _, opened = directory.put_connection(game, ROOM, CONNECTION, JOIN_TOKEN, JOIN_BODY)
    assert set(opened["ice"]) == {"stun", "turn"}
    assert set(opened["ice"]["stun"]) == {"host", "port"}
    assert set(opened["ice"]["turn"]) == {
        "host",
        "port",
        "username",
        "password",
        "expires_at",
    }
    clock.now += 30
    offered = directory.put_offer(game, ROOM, CONNECTION, JOIN_TOKEN, "o")
    assert (offered["ice"], offered["expires_at"]) == (
        opened["ice"],
        opened["expires_at"],
    )


def test_an_answer_body_is_one_sdp_or_one_refusal_name() -> None:
    assert read_answer({"sdp": "a"}) == {"sdp": "a"}
    for name in ("full", "not_joinable", "connection_failed"):
        assert read_answer({"error": name}) == {"error": name}
    for body in ({}, {"sdp": ""}, {"error": "closed"}, {"sdp": "a", "error": "full"}):
        assert refusal(read_answer, body) == (400, "invalid_request")


async def test_a_join_wakes_the_hosts_waiting_poll(directory: Directory) -> None:
    directory.long_poll_seconds = 5
    game = directory.issue_game()
    directory.put_room(game, ROOM, HOST_TOKEN, ROOM_BODY)
    first = await directory.poll_connections(game, ROOM, HOST_TOKEN, None)
    waiting = asyncio.create_task(
        directory.poll_connections(game, ROOM, HOST_TOKEN, first["cursor"])
    )
    await asyncio.sleep(0.05)
    assert not waiting.done()
    directory.put_connection(game, ROOM, CONNECTION, JOIN_TOKEN, JOIN_BODY)
    polled = await asyncio.wait_for(waiting, 1)
    assert polled["cursor"] != first["cursor"]
    assert [signal["connection_id"] for signal in polled["connections"]] == [CONNECTION]


async def test_the_hosts_poll_lists_connections_by_id(directory: Directory) -> None:
    game = directory.issue_game()
    directory.put_room(game, ROOM, HOST_TOKEN, ROOM_BODY)
    for connection_id in ("join-b", "join-a"):
        directory.put_connection(game, ROOM, connection_id, JOIN_TOKEN, JOIN_BODY)
    polled = await directory.poll_connections(game, ROOM, HOST_TOKEN, None)
    listed = [signal["connection_id"] for signal in polled["connections"]]
    assert listed == ["join-a", "join-b"]


async def test_a_current_cursor_waits_the_long_poll_then_returns_unchanged(
    directory: Directory,
) -> None:
    game = directory.issue_game()
    directory.put_room(game, ROOM, HOST_TOKEN, ROOM_BODY)
    first = await directory.poll_connections(game, ROOM, HOST_TOKEN, None)
    loop = asyncio.get_running_loop()
    started = loop.time()
    again = await directory.poll_connections(game, ROOM, HOST_TOKEN, first["cursor"])
    assert loop.time() - started >= 0.2
    assert again == first


async def test_an_answer_wakes_the_joiners_waiting_poll(directory: Directory) -> None:
    directory.long_poll_seconds = 5
    game = directory.issue_game()
    directory.put_room(game, ROOM, HOST_TOKEN, ROOM_BODY)
    directory.put_connection(game, ROOM, CONNECTION, JOIN_TOKEN, JOIN_BODY)
    directory.put_offer(game, ROOM, CONNECTION, JOIN_TOKEN, "o")
    waiting = asyncio.create_task(
        directory.poll_connection(game, ROOM, CONNECTION, JOIN_TOKEN)
    )
    await asyncio.sleep(0.05)
    assert not waiting.done()
    directory.put_answer(game, ROOM, CONNECTION, HOST_TOKEN, {"error": "full"})
    signal = await asyncio.wait_for(waiting, 1)
    assert (signal["state"], signal["answer"]) == ("rejected", {"error": "full"})


async def test_retiring_a_game_ends_its_waiting_polls(directory: Directory) -> None:
    directory.long_poll_seconds = 5
    game = directory.issue_game()
    directory.put_room(game, ROOM, HOST_TOKEN, ROOM_BODY)
    first = await directory.poll_connections(game, ROOM, HOST_TOKEN, None)
    waiting = asyncio.create_task(
        directory.poll_connections(game, ROOM, HOST_TOKEN, first["cursor"])
    )
    await asyncio.sleep(0.05)
    directory.retire_game(game)
    with pytest.raises(DirectoryError) as caught:
        await asyncio.wait_for(waiting, 1)
    assert caught.value.name == "not_found"


async def test_no_log_line_carries_a_token_or_a_whole_game_id(
    directory: Directory, caplog: pytest.LogCaptureFixture
) -> None:
    caplog.set_level(logging.DEBUG, logger="jedimaster.matchmaker")
    game = directory.issue_game()
    directory.put_room(game, ROOM, HOST_TOKEN, ROOM_BODY)
    directory.list_rooms(game)
    await directory.poll_connections(game, ROOM, HOST_TOKEN, "v0")
    directory.put_connection(game, ROOM, CONNECTION, JOIN_TOKEN, JOIN_BODY)
    directory.put_offer(game, ROOM, CONNECTION, JOIN_TOKEN, "o")
    directory.put_answer(game, ROOM, CONNECTION, HOST_TOKEN, {"sdp": "a"})
    await directory.poll_connection(game, ROOM, CONNECTION, JOIN_TOKEN)
    directory.delete_connection(game, ROOM, CONNECTION, JOIN_TOKEN)
    directory.delete_room(game, ROOM, HOST_TOKEN)
    directory.retire_game(game)
    assert game[:8] in caplog.text
    for secret in (game, HOST_TOKEN, JOIN_TOKEN):
        assert secret not in caplog.text
