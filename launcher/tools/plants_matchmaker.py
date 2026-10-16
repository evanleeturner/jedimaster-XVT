"""The planted faults of the host's matchmaker: the directory, its server, its certificate.

Purpose:
    Break one rule of ``jedimaster/matchmaker`` per plant: the issued ids
    and what a stranger gets, the leases and the caps, the tokens, the
    offer and answer kept once, the waits, the field sets, the log's
    secrets, the server's routes and limits, the certificate made once and
    its fingerprint, and the command's defaults and exits, so each test of
    them is shown to fail on a fault in the code it guards.

Flow:
    ``plant_faults`` joins this table with the others and applies each plant
    alone: write ``new`` over ``old`` in ``path``, run the suite, restore,
    and check that every test in ``expect`` failed. An ``expect`` entry
    matches a test by prefix.

Invariants:
    - Ids are unique across all tables, all starting ``mm-``.
    - Each ``old`` text occurs exactly once in its file.
    - Not planted, for lack of a fence that can tell: the 120 s connection
      lease's exact length (only its lapse is fenced) and the TLS settings
      beyond the lowest version.

Call:
    ``from plants_matchmaker import MATCHMAKER_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import CLI
from plants_common import Plant

logger = logging.getLogger(__name__)

DIR = "jedimaster/matchmaker/directory.py"
SRV = "jedimaster/matchmaker/server.py"
CRT = "jedimaster/matchmaker/certificate.py"
MCL = "jedimaster/matchmaker/cli.py"
TD = "tests/test_matchmaker_directory.py::"
TS = "tests/test_matchmaker_server.py::"
TC = "tests/test_matchmaker_certificate.py::"
TL = "tests/test_matchmaker_cli.py::"
SERVES = TL + "test_the_command_serves_its_games_over_tls_with_the_printed_pin"


def plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    """Return the plant ``mm-<pid>``."""
    return Plant(f"mm-{pid}", path, old, new, expect)


MATCHMAKER_PLANTS: list[Plant] = [
    plant(
        "id-upper-case",
        DIR,
        "game = str(uuid.uuid4())",
        "game = str(uuid.uuid4()).upper()",
        TD + "test_a_game_id_is_fresh_lowercase_guid_text",
        SERVES,
    ),
    plant(
        "id-not-fresh",
        DIR,
        "game = str(uuid.uuid4())",
        "game = str(uuid.UUID(int=7))",
        TD + "test_a_game_id_is_fresh_lowercase_guid_text",
        TD + "test_rooms_of_one_game_are_not_listed_under_another",
    ),
    plant(
        "list-any-game",
        DIR,
        "sorted(self.games.get(game, {}).items())",
        "sorted(next(iter(self.games.values()), {}).items())",
        TD + "test_rooms_of_one_game_are_not_listed_under_another",
        TS + "test_a_stranger_lists_nothing_and_is_refused_the_rest",
    ),
    plant(
        "list-unsorted",
        DIR,
        "sorted(self.games.get(game, {}).items())",
        "self.games.get(game, {}).items()",
        TD + "test_the_rooms_of_a_game_are_capped_and_a_lapsed_one_frees_a_place",
    ),
    plant(
        "serve-any-game",
        DIR,
        "rooms = self.games.get(game)\n",
        "rooms = self.games.setdefault(game, {})\n",
        TD + "test_a_game_not_issued_lists_empty_and_refuses_the_rest",
        TS + "test_a_stranger_lists_nothing_and_is_refused_the_rest",
    ),
    plant(
        "rooms-across-games",
        DIR,
        "room = self._rooms(game).get(room_id)\n        if room is None or",
        "room = next((r[room_id] for r in self.games.values() if room_id in r), None)"
        "\n        if room is None or",
        TD + "test_a_game_not_issued_lists_empty_and_refuses_the_rest",
        TD + "test_polls_under_a_game_not_issued_are_refused_at_once",
    ),
    plant(
        "room-field-dropped",
        DIR,
        '"expires_at": timestamp(room.expires_at),',
        "",
        TS + "test_host_and_join_walk_the_client_protocol",
    ),
    plant(
        "joiner-shown-offer",
        DIR,
        '        if host:\n            signal["offer"]',
        '        if True:\n            signal["offer"]',
        TS + "test_host_and_join_walk_the_client_protocol",
    ),
    plant(
        "retire-keeps-rooms",
        DIR,
        "rooms = self.games.pop(game, None)",
        "rooms = self.games.get(game)",
        TD + "test_a_retired_game_loses_its_rooms",
        TD + "test_retiring_a_game_ends_its_waiting_polls",
    ),
    plant(
        "retire-wakes-nobody",
        DIR,
        '        self._wake()\n        logger.info("game retired',
        '        logger.info("game retired',
        TD + "test_retiring_a_game_ends_its_waiting_polls",
    ),
    plant(
        "room-lease-longer",
        DIR,
        "room = Room(room_id, token, body, now + ROOM_LEASE_SECONDS)",
        "room = Room(room_id, token, body, now + ROOM_LEASE_SECONDS + 1)",
        TD + "test_a_room_lapses_a_lease_after_its_last_put",
    ),
    plant(
        "room-not-renewed",
        DIR,
        "room.expires_at = now + ROOM_LEASE_SECONDS",
        "room.expires_at = room.expires_at",
        TD + "test_a_room_lapses_a_lease_after_its_last_put",
        TD + "test_a_full_room_frees_a_seat_when_a_connection_lapses",
    ),
    plant(
        "lapsed-room-served",
        DIR,
        "        if room is None or room.expires_at <= self.clock():\n"
        '            raise DirectoryError(404, "not_found")\n        return room\n',
        "        if room is None:\n"
        '            raise DirectoryError(404, "not_found")\n        return room\n',
        TD + "test_a_room_lapses_a_lease_after_its_last_put",
    ),
    plant(
        "lapsed-connection-served",
        DIR,
        "if connection is None or connection.expires_at <= self.clock():",
        "if connection is None:",
        TD + "test_a_full_room_frees_a_seat_when_a_connection_lapses",
    ),
    plant(
        "room-cap-off-by-one",
        DIR,
        "if len(rooms) >= MAX_ROOMS_PER_GAME:",
        "if len(rooms) > MAX_ROOMS_PER_GAME:",
        TD + "test_the_rooms_of_a_game_are_capped_and_a_lapsed_one_frees_a_place",
    ),
    plant(
        "lapsed-rooms-kept",
        DIR,
        "            del rooms[lapsed]\n",
        "            pass\n",
        TD + "test_the_rooms_of_a_game_are_capped_and_a_lapsed_one_frees_a_place",
    ),
    plant(
        "lapsed-connections-kept",
        DIR,
        "            del room.connections[lapsed]\n",
        "            pass\n",
        TD + "test_a_full_room_frees_a_seat_when_a_connection_lapses",
    ),
    plant(
        "seats-off-by-one",
        DIR,
        "if len(room.connections) >= MAX_PLAYERS:",
        "if len(room.connections) > MAX_PLAYERS:",
        TD + "test_a_full_room_frees_a_seat_when_a_connection_lapses",
    ),
    plant(
        "room-protocol-unchecked",
        DIR,
        'if body.get("protocol") != PROTOCOL:\n',
        "if False:\n",
        TD + "test_the_joiner_needs_the_rooms_protocol_version_and_joinable",
    ),
    plant(
        "join-version-unchecked",
        DIR,
        'room.body.get("game_version")',
        'body.get("game_version")',
        TD + "test_the_joiner_needs_the_rooms_protocol_version_and_joinable",
        TS + "test_refusals_carry_the_client_error_names",
    ),
    plant(
        "joinable-unchecked",
        DIR,
        'if not room.body.get("joinable"):',
        "if False:",
        TD + "test_the_joiner_needs_the_rooms_protocol_version_and_joinable",
        TS + "test_refusals_carry_the_client_error_names",
    ),
    plant(
        "room-token-unchecked",
        DIR,
        "        if room.token != token:\n"
        '            raise DirectoryError(401, "unauthorized")\n'
        "        if body != room.body:",
        "        if body != room.body:",
        TD + "test_only_the_tokens_that_made_a_thing_change_it",
        TS + "test_refusals_carry_the_client_error_names",
    ),
    plant(
        "offer-replaced",
        DIR,
        "if connection.offer is not None and connection.offer != sdp:",
        "if False:",
        TD + "test_an_offer_and_an_answer_are_kept_once",
    ),
    plant(
        "answer-replaced",
        DIR,
        "if connection.answer is not None and connection.answer != answer:",
        "if False:",
        TD + "test_an_offer_and_an_answer_are_kept_once",
    ),
    plant(
        "refusal-reads-answered",
        DIR,
        '"rejected" if "error" in answer else "answered"',
        '"answered"',
        TD + "test_an_answer_wakes_the_joiners_waiting_poll",
        TS + "test_a_refused_join_reads_as_rejected",
    ),
    plant(
        "change-wakes-nobody",
        DIR,
        "        room.version += 1\n        self._wake()\n",
        "        room.version += 1\n",
        TD + "test_a_join_wakes_the_hosts_waiting_poll",
        TD + "test_an_answer_wakes_the_joiners_waiting_poll",
    ),
    plant(
        "connection-expiry-moves",
        DIR,
        '"expires_at": timestamp(connection.expires_at),',
        '"expires_at": timestamp(self.clock()),',
        TD + "test_the_joiners_ice_block_and_expiry_never_change",
    ),
    plant(
        "connections-unsorted",
        DIR,
        "sorted(room.connections.items())",
        "room.connections.items()",
        TD + "test_the_hosts_poll_lists_connections_by_id",
    ),
    plant(
        "current-cursor-returns-at-once",
        DIR,
        "if cursor != current or remaining <= 0:",
        "if True:",
        TD + "test_a_current_cursor_waits_the_long_poll_then_returns_unchanged",
    ),
    plant(
        "joiner-poll-ignores-answer",
        DIR,
        'if connection.state in ("answered", "rejected") or remaining <= 0:',
        "if remaining <= 0:",
        TD + "test_an_answer_wakes_the_joiners_waiting_poll",
    ),
    plant(
        "answer-body-loose",
        DIR,
        '    if len(body) != 1:\n        raise DirectoryError(400, "invalid_request")\n'
        '    if isinstance(body.get("sdp"), str) and body["sdp"]:',
        '    if isinstance(body.get("sdp"), str) and body["sdp"]:',
        TD + "test_an_answer_body_is_one_sdp_or_one_refusal_name",
    ),
    plant(
        "log-whole-game-id",
        DIR,
        "return game[:SHOWN_ID_CHARACTERS]",
        "return game",
        TD + "test_no_log_line_carries_a_token_or_a_whole_game_id",
        TS + "test_refusals_warn_only_under_an_issued_game",
    ),
    plant(
        "log-a-token",
        DIR,
        'logger.info("room removed room=%s", room_id)',
        'logger.info("room removed room=%s token=%s", room_id, token)',
        TD + "test_no_log_line_carries_a_token_or_a_whole_game_id",
    ),
    plant(
        "body-cap-doubled",
        SRV,
        "client_max_size=MAX_BODY_BYTES",
        "client_max_size=MAX_BODY_BYTES * 2",
        TS + "test_a_body_over_the_clients_largest_is_refused",
    ),
    plant(
        "body-cap-constant",
        SRV,
        "MAX_BODY_BYTES = 8192",
        "MAX_BODY_BYTES = 16384",
        TS + "test_a_body_over_the_clients_largest_is_refused",
    ),
    plant(
        "too-large-misnamed",
        SRV,
        'raise DirectoryError(413, "body_too_large") from err',
        'raise DirectoryError(400, "invalid_request") from err',
        TS + "test_a_body_over_the_clients_largest_is_refused",
    ),
    plant(
        "body-any-json",
        SRV,
        '    if not isinstance(body, dict):\n        raise DirectoryError(400, "invalid_request")\n    return body\n',
        "    return body\n",
        TS + "test_a_body_that_is_not_one_json_object_is_refused",
    ),
    plant(
        "no-bearer-read",
        SRV,
        'return header[7:] if header.startswith("Bearer ") else ""',
        'return ""',
        TS + "test_refusals_carry_the_client_error_names",
    ),
    plant(
        "any-prefix",
        SRV,
        'parts[:2] != ["v1", "apps"]',
        'parts[:1] != ["v1"]',
        TS + "test_other_paths_and_methods_answer_json_refusals",
    ),
    plant(
        "any-collection",
        SRV,
        ' or parts[3] != "rooms"',
        "",
        TS + "test_other_paths_and_methods_answer_json_refusals",
    ),
    plant(
        "unknown-method-not-found",
        SRV,
        '        raise DirectoryError(404, "not_found")\n'
        '    raise DirectoryError(405, "method_not_allowed")\n',
        '        raise DirectoryError(404, "not_found")\n'
        '    raise DirectoryError(404, "not_found")\n',
        TS + "test_other_paths_and_methods_answer_json_refusals",
    ),
    plant(
        "every-refusal-warns",
        SRV,
        "level = logging.WARNING if game in directory.games else logging.DEBUG",
        "level = logging.WARNING",
        TS + "test_refusals_warn_only_under_an_issued_game",
    ),
    plant(
        "refusal-logs-whole-id",
        SRV,
        'path = "/".join(["", *parts[:2], shown(game), *parts[3:]])',
        'path = "/".join(["", *parts[:2], game, *parts[3:]])',
        TS + "test_refusals_warn_only_under_an_issued_game",
    ),
    plant(
        "framework-named",
        SRV,
        'response.headers["Server"] = SERVER_NAME',
        "return",
        TS + "test_the_server_does_not_name_its_framework",
    ),
    plant(
        "access-log-on",
        SRV,
        "web.AppRunner(make_app(directory), access_log=None)",
        "web.AppRunner(make_app(directory))",
        SERVES,
    ),
    plant(
        "same-key-everywhere",
        CRT,
        "key = ec.generate_private_key(ec.SECP256R1())",
        "key = ec.derive_private_key(7, ec.SECP256R1())",
        TC + "test_two_installs_get_two_fingerprints",
    ),
    plant(
        "key-readable",
        CRT,
        "KEY_FILE_MODE = 0o600",
        "KEY_FILE_MODE = 0o644",
        TC + "test_only_the_owner_may_read_the_key",
    ),
    plant(
        "lone-file-remade",
        CRT,
        "if have_cert != have_key:",
        "if False:",
        TC + "test_a_lone_file_is_refused_and_left_as_it_is",
    ),
    plant(
        "pair-always-made",
        CRT,
        "    if have_cert:\n",
        "    if False:\n",
        TC + "test_a_pair_is_made_once_and_then_loaded_unchanged",
        TC + "test_a_pair_that_does_not_read_is_refused",
    ),
    plant(
        "key-not-matched",
        CRT,
        'raise CertificateError(f"{key_path} is not the key of {cert_path}")',
        "pass",
        TC + "test_a_pair_that_does_not_read_is_refused",
    ),
    plant(
        "pin-prefix",
        CRT,
        'PIN_PREFIX = "sha256//"',
        'PIN_PREFIX = "sha256/"',
        TC + "test_a_pair_is_made_once_and_then_loaded_unchanged",
        TC + "test_the_fingerprint_is_the_public_key_the_server_shows",
        SERVES,
    ),
    plant(
        "pin-hashes-other-bytes",
        CRT,
        "hashlib.sha256(spki).digest()",
        "hashlib.sha256(spki + b'.').digest()",
        TC + "test_the_fingerprint_is_the_public_key_the_server_shows",
        SERVES,
    ),
    plant(
        "tls-1-0-allowed",
        CRT,
        "context.minimum_version = ssl.TLSVersion.TLSv1_2",
        "context.minimum_version = ssl.TLSVersion.TLSv1",
        TC + "test_the_server_takes_tls_1_2_or_newer",
    ),
    plant(
        "default-port",
        MCL,
        "DEFAULT_PORT = 8781",
        "DEFAULT_PORT = 8780",
        TL + "test_the_command_defaults_to_one_game_on_port_8781",
    ),
    plant(
        "certificate-error-exits-0",
        MCL,
        '        logger.error("%s", err)\n        return 2\n',
        '        logger.error("%s", err)\n        return 0\n',
        TL + "test_a_lone_key_file_stops_the_command",
    ),
    plant(
        "busy-port-exits-0",
        MCL,
        '        logger.error("cannot serve on port %d: %s", args.port, err)\n'
        "        return 2\n",
        '        logger.error("cannot serve on port %d: %s", args.port, err)\n'
        "        return 0\n",
        TL + "test_a_port_in_use_stops_the_command",
    ),
    plant(
        "wrong-pin-printed",
        MCL,
        'f"fingerprint {certificate.fingerprint}"',
        'f"fingerprint {certificate.fingerprint[1:]}"',
        SERVES,
    ),
    plant(
        "command-unregistered",
        CLI,
        '    "matchmaker": matchmaker_cli,\n',
        "",
        SERVES,
        TL + "test_a_lone_key_file_stops_the_command",
    ),
]
