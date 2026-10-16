"""The matchmaker's directory: the rooms of the games this host issued.

Purpose:
    The games of a network battle find each other through a directory: the
    host's game lists a room, a joiner's game opens a connection on it, and
    the two trade connection offers through the directory before they talk
    directly. This one runs on the host's own computer and serves only the
    games the host's launcher issued, each under a secret application id, so
    nothing of a battle is reachable without its id.

Flow:
    1. ``issue_game`` draws a fresh id; the launcher writes it into every
       player's launch description as the network ``game``.
    2. The host's game PUTs its room under that id and renews it every 15 s;
       a joiner's game lists the rooms, PUTs a connection, then its offer.
    3. The host's game long-polls its room's connections, sees the offer and
       PUTs an answer; the joiner long-polls its connection and reads it.
    4. Either side DELETEs what it made; a room lapses 60 s after its last
       PUT and a connection 120 s after it opened; ``retire_game`` forgets an
       id and its rooms.

Invariants:
    - Every answer carries exactly the fields Aeron's directory client
      (protocol 2) checks for, in its names: a room's 13, a joiner's
      connection 5, the host's 6. A connection's expiry and ICE block never
      change after it opens, because the joiner refuses a reply where they
      differ.
    - Only issued ids have rooms: a list under any other id is empty, and
      every other request under one is refused as not_found.
    - A game holds at most 4 live rooms and a room at most 8 live
      connections; lapsed ones are dropped before counting.
    - Rooms and connections in a list are ordered by their id text.
    - Bearer tokens are compared and never logged; a game id is logged by
      its first 8 characters only.
    - State lives in memory: a restart forgets every game and room.
    - One event loop: the methods that wait are coroutines, the others never
      wait, and every change wakes every waiting poll. Leases follow
      ``clock`` (wall seconds); waits follow the event loop's own timer.

Call:
    ``directory = Directory("localhost"); game = directory.issue_game()``
"""

from __future__ import annotations

import asyncio
import contextlib
import logging
import time
import uuid
from collections.abc import Callable
from dataclasses import dataclass
from dataclasses import field

logger = logging.getLogger(__name__)

PROTOCOL = 2
ROOM_LEASE_SECONDS = 60
CONNECTION_LEASE_SECONDS = 120
LONG_POLL_SECONDS = 25
MAX_PLAYERS = 8
MAX_ROOMS_PER_GAME = 4
# The ICE servers the games are told about. The client needs a STUN and a
# TURN entry; where nothing answers on this port, it gives up on them and
# connects by its own network addresses.
ICE_PORT = 3478
ICE_USERNAME = "none"
ICE_PASSWORD = "none"
ICE_LEASE_SECONDS = 86400
REJECTIONS = ("full", "not_joinable", "connection_failed")
SHOWN_ID_CHARACTERS = 8


class DirectoryError(Exception):
    """A refusal the client understands: an HTTP status and an error name."""

    def __init__(self, status: int, name: str) -> None:
        super().__init__(f"{status} {name}")
        self.status = status
        self.name = name


@dataclass
class Connection:
    """One join attempt: the joiner's token, its offer and the host's answer."""

    connection_id: str
    token: str
    expires_at: float
    state: str = "waiting_offer"
    offer: str | None = None
    answer: dict[str, str] | None = None


@dataclass
class Room:
    """One listed game room: the host's token and the fields it last sent."""

    room_id: str
    token: str
    body: dict[str, object]
    expires_at: float
    version: int = 1
    connections: dict[str, Connection] = field(default_factory=dict)


def timestamp(seconds: float) -> str:
    """Return the client's timestamp form: UTC, whole seconds, a Z suffix."""
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(seconds))


def shown(game: str) -> str:
    """Return the part of a game id a log line may show: its first 8 characters."""
    return game[:SHOWN_ID_CHARACTERS]


def read_answer(body: dict[str, object]) -> dict[str, str]:
    """Return the host's answer body as stored: one sdp or one refusal name.

    Raises ``DirectoryError`` invalid_request for any other body.
    """
    if len(body) != 1:
        raise DirectoryError(400, "invalid_request")
    if isinstance(body.get("sdp"), str) and body["sdp"]:
        return {"sdp": body["sdp"]}
    if body.get("error") in REJECTIONS:
        return {"error": body["error"]}
    raise DirectoryError(400, "invalid_request")


class Directory:
    """The issued games and their rooms, for one event loop."""

    def __init__(
        self,
        ice_host: str,
        clock: Callable[[], float] = time.time,
        long_poll_seconds: float = LONG_POLL_SECONDS,
    ) -> None:
        """Hold no games yet; tell the games ``ice_host`` as STUN and TURN."""
        self.games: dict[str, dict[str, Room]] = {}
        self.clock = clock
        self.long_poll_seconds = long_poll_seconds
        self._changed = asyncio.Event()
        self.ice = {
            "stun": {"host": ice_host, "port": ICE_PORT},
            "turn": {
                "host": ice_host,
                "port": ICE_PORT,
                "username": ICE_USERNAME,
                "password": ICE_PASSWORD,
                "expires_at": timestamp(clock() + ICE_LEASE_SECONDS),
            },
        }

    def issue_game(self) -> str:
        """Return a fresh game id, the client's GUID text, and serve it."""
        game = str(uuid.uuid4())
        self.games[game] = {}
        logger.info("game issued game=%s", shown(game))
        return game

    def retire_game(self, game: str) -> None:
        """Forget ``game`` and its rooms; a game never issued is ignored."""
        rooms = self.games.pop(game, None)
        if rooms is None:
            return
        self._wake()
        logger.info("game retired game=%s rooms=%d", shown(game), len(rooms))

    def _wake(self) -> None:
        """Wake every waiting poll; later waits wait for the next change."""
        self._changed.set()
        self._changed = asyncio.Event()

    async def _wait(self, seconds: float) -> None:
        """Return at the next change, or after ``seconds``."""
        with contextlib.suppress(TimeoutError):
            await asyncio.wait_for(self._changed.wait(), seconds)

    def _rooms(self, game: str) -> dict[str, Room]:
        """Return an issued game's rooms; raise not_found for any other id."""
        rooms = self.games.get(game)
        if rooms is None:
            raise DirectoryError(404, "not_found")
        return rooms

    def _live_room(self, game: str, room_id: str) -> Room:
        """Return a room that has not lapsed; raise not_found otherwise."""
        room = self._rooms(game).get(room_id)
        if room is None or room.expires_at <= self.clock():
            raise DirectoryError(404, "not_found")
        return room

    def _connection(
        self, game: str, room_id: str, connection_id: str
    ) -> tuple[Room, Connection]:
        """Return a live room and one of its live connections, or raise not_found."""
        room = self._live_room(game, room_id)
        connection = room.connections.get(connection_id)
        if connection is None or connection.expires_at <= self.clock():
            raise DirectoryError(404, "not_found")
        return room, connection

    def _bump(self, room: Room) -> None:
        """Mark the room changed and wake every waiting poll."""
        room.version += 1
        self._wake()

    def room_json(self, room: Room) -> dict[str, object]:
        """Return the 13-field room the client reads back."""
        return {
            "room_id": room.room_id,
            "expires_at": timestamp(room.expires_at),
            **room.body,
        }

    def signal_json(self, connection: Connection, host: bool) -> dict[str, object]:
        """Return a connection as the host (6 fields) or the joiner (5) reads it."""
        signal: dict[str, object] = {
            "connection_id": connection.connection_id,
            "expires_at": timestamp(connection.expires_at),
            "ice": self.ice,
            "state": connection.state,
            "answer": connection.answer,
        }
        if host:
            signal["offer"] = (
                None if connection.offer is None else {"sdp": connection.offer}
            )
        return signal

    def list_rooms(self, game: str) -> dict[str, object]:
        """Return the live rooms of ``game`` by room id; none for an id not issued."""
        now = self.clock()
        rooms = [
            self.room_json(room)
            for _, room in sorted(self.games.get(game, {}).items())
            if room.expires_at > now
        ]
        logger.debug("list game=%s rooms=%d", shown(game), len(rooms))
        return {"rooms": rooms}

    def put_room(
        self, game: str, room_id: str, token: str, body: dict[str, object]
    ) -> tuple[int, dict[str, object]]:
        """Create or renew a room; only its first token may change it.

        Returns 201 and the room when it is new (or had lapsed), else 200.
        """
        rooms = self._rooms(game)
        if body.get("protocol") != PROTOCOL:
            raise DirectoryError(409, "incompatible")
        now = self.clock()
        for lapsed in [key for key, room in rooms.items() if room.expires_at <= now]:
            del rooms[lapsed]
        room = rooms.get(room_id)
        if room is None:
            if len(rooms) >= MAX_ROOMS_PER_GAME:
                raise DirectoryError(503, "capacity")
            room = Room(room_id, token, body, now + ROOM_LEASE_SECONDS)
            rooms[room_id] = room
            self._wake()
            logger.info(
                "room listed game=%s room=%s players=%s/%s",
                shown(game),
                room_id,
                body.get("players"),
                body.get("max_players"),
            )
            logger.debug("room put room=%s body=%s", room_id, body)
            return 201, self.room_json(room)
        if room.token != token:
            raise DirectoryError(401, "unauthorized")
        if body != room.body:
            logger.info(
                "room changed room=%s revision=%s state=%s joinable=%s",
                room_id,
                body.get("revision"),
                body.get("state"),
                body.get("joinable"),
            )
        room.body = body
        room.expires_at = now + ROOM_LEASE_SECONDS
        logger.debug("room put room=%s body=%s", room_id, body)
        return 200, self.room_json(room)

    def delete_room(self, game: str, room_id: str, token: str) -> None:
        """Remove a room when its host asks; a missing room is not an error."""
        rooms = self._rooms(game)
        room = rooms.get(room_id)
        if room is None:
            return
        if room.token != token:
            raise DirectoryError(401, "unauthorized")
        del rooms[room_id]
        self._wake()
        logger.info("room removed room=%s", room_id)

    async def poll_connections(
        self, game: str, room_id: str, token: str, cursor: str | None
    ) -> dict[str, object]:
        """Return the room's connections once they differ from ``cursor``.

        Waits up to ``long_poll_seconds`` while the cursor is current, then
        returns the unchanged list with the same cursor.
        """
        loop = asyncio.get_running_loop()
        deadline = loop.time() + self.long_poll_seconds
        while True:
            room = self._live_room(game, room_id)
            if room.token != token:
                raise DirectoryError(401, "unauthorized")
            current = f"v{room.version}"
            remaining = deadline - loop.time()
            if cursor != current or remaining <= 0:
                break
            await self._wait(remaining)
        now = self.clock()
        connections = [
            self.signal_json(connection, host=True)
            for _, connection in sorted(room.connections.items())
            if connection.expires_at > now
        ]
        logger.debug(
            "host poll room=%s cursor=%s connections=%s", room_id, current, connections
        )
        return {"cursor": current, "connections": connections}

    def put_connection(
        self,
        game: str,
        room_id: str,
        connection_id: str,
        token: str,
        body: dict[str, object],
    ) -> tuple[int, dict[str, object]]:
        """Open a join attempt on a room that is joinable and not full.

        Returns 201 and the joiner's view when it is new; the same
        connection asked again by its own token returns 200 and its view.
        """
        room = self._live_room(game, room_id)
        existing = room.connections.get(connection_id)
        if existing is not None:
            if existing.token != token:
                raise DirectoryError(401, "unauthorized")
            return 200, self.signal_json(existing, host=False)
        if body.get("protocol") != PROTOCOL or body.get(
            "game_version"
        ) != room.body.get("game_version"):
            raise DirectoryError(409, "incompatible")
        if not room.body.get("joinable"):
            raise DirectoryError(409, "not_joinable")
        now = self.clock()
        for lapsed in [
            key
            for key, connection in room.connections.items()
            if connection.expires_at <= now
        ]:
            del room.connections[lapsed]
        if len(room.connections) >= MAX_PLAYERS:
            raise DirectoryError(409, "full")
        connection = Connection(connection_id, token, now + CONNECTION_LEASE_SECONDS)
        room.connections[connection_id] = connection
        self._bump(room)
        logger.info("join opened room=%s connection=%s", room_id, connection_id)
        return 201, self.signal_json(connection, host=False)

    def put_offer(
        self, game: str, room_id: str, connection_id: str, token: str, sdp: str
    ) -> dict[str, object]:
        """Store the joiner's offer once; a different second offer conflicts."""
        room, connection = self._connection(game, room_id, connection_id)
        if connection.token != token:
            raise DirectoryError(401, "unauthorized")
        if connection.offer is not None and connection.offer != sdp:
            raise DirectoryError(409, "conflict")
        if connection.offer is None:
            connection.offer = sdp
            connection.state = "waiting_answer"
            self._bump(room)
            logger.info("offer stored connection=%s", connection_id)
        logger.debug("offer connection=%s sdp_bytes=%d", connection_id, len(sdp))
        return self.signal_json(connection, host=False)

    def put_answer(
        self,
        game: str,
        room_id: str,
        connection_id: str,
        token: str,
        answer: dict[str, str],
    ) -> None:
        """Store the host's answer or refusal once; the room's token must match."""
        room, connection = self._connection(game, room_id, connection_id)
        if room.token != token:
            raise DirectoryError(401, "unauthorized")
        if connection.answer is not None and connection.answer != answer:
            raise DirectoryError(409, "conflict")
        if connection.answer is None:
            connection.answer = answer
            connection.state = "rejected" if "error" in answer else "answered"
            self._bump(room)
            logger.info(
                "answer stored connection=%s state=%s", connection_id, connection.state
            )
        logger.debug("answer connection=%s answer=%s", connection_id, answer)

    async def poll_connection(
        self, game: str, room_id: str, connection_id: str, token: str
    ) -> dict[str, object]:
        """Return the joiner's connection once answered, or after the wait."""
        loop = asyncio.get_running_loop()
        deadline = loop.time() + self.long_poll_seconds
        while True:
            _, connection = self._connection(game, room_id, connection_id)
            if connection.token != token:
                raise DirectoryError(401, "unauthorized")
            remaining = deadline - loop.time()
            if connection.state in ("answered", "rejected") or remaining <= 0:
                break
            await self._wait(remaining)
        logger.debug(
            "join poll connection=%s state=%s", connection_id, connection.state
        )
        return self.signal_json(connection, host=False)

    def delete_connection(
        self, game: str, room_id: str, connection_id: str, token: str
    ) -> None:
        """Remove a join attempt for its joiner or the room's host."""
        room = self._rooms(game).get(room_id)
        connection = room.connections.get(connection_id) if room else None
        if connection is None:
            return
        if token not in (connection.token, room.token):
            raise DirectoryError(401, "unauthorized")
        del room.connections[connection_id]
        self._bump(room)
        logger.info("join closed room=%s connection=%s", room_id, connection_id)
