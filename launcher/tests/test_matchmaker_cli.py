"""Fences for ``python -m jedimaster matchmaker``: run as a real process, over TLS.

The command is started as the host would start it, on a free port of
127.0.0.1, its log at DEBUG; the test reads its announced lines, asks it
over TLS as a game would, and checks the certificate it shows against the
fingerprint it printed.
"""

from __future__ import annotations

import base64
import hashlib
import http.client
import json
import os
import re
import signal
import socket
import ssl
import subprocess
import sys
import threading
from pathlib import Path

import pytest
from cryptography import x509
from cryptography.hazmat.primitives import serialization

from jedimaster.__main__ import _parser
from jedimaster.matchmaker.certificate import CERT_FILE_NAME
from jedimaster.matchmaker.certificate import KEY_FILE_NAME
from jedimaster.matchmaker.cli import DEFAULT_PORT

LAUNCHER = Path(__file__).resolve().parents[1]
STRANGER = "11111111-2222-4333-8444-555555555555"
GUID_TEXT = r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"
START_SECONDS = 20


def start(
    settings: Path, *options: str, ctrl_c: signal.Handlers = signal.SIG_DFL
) -> subprocess.Popen:
    """Start the command on a free port of 127.0.0.1 with its log at DEBUG.

    Outside Windows the command starts with SIGINT set to ``ctrl_c``, by
    default as a terminal starts it, whatever the test runner's own setting:
    a runner started as a script's background job has SIGINT ignored, and
    its children inherit that.
    """
    env = {**os.environ, "PYTHONPATH": str(LAUNCHER)}
    command = [sys.executable, "-m", "jedimaster", "-vv", "matchmaker"]
    command += ["--settings", str(settings), "--host", "127.0.0.1", "--port", "0"]
    return subprocess.Popen(
        [*command, *options],
        cwd=LAUNCHER,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        preexec_fn=None
        if sys.platform == "win32"
        else lambda: signal.signal(signal.SIGINT, ctrl_c),
    )


def announced(process: subprocess.Popen, games: int) -> dict[str, list[str]]:
    """Read the announced lines: one port, one fingerprint, ``games`` games.

    A command that has not announced them within ``START_SECONDS`` is
    killed, so the read ends and the test fails instead of waiting forever.
    """
    watchdog = threading.Timer(START_SECONDS, process.kill)
    watchdog.start()
    lines: dict[str, list[str]] = {"port": [], "fingerprint": [], "game": []}
    try:
        while len(lines["game"]) < games or not lines["fingerprint"]:
            line = process.stdout.readline()
            assert line, process.stderr.read()
            name, value = line.split()
            lines[name].append(value)
    finally:
        watchdog.cancel()
    return lines


def ended(process: subprocess.Popen, signum: int | None = None) -> tuple[str, str]:
    """Send ``signum`` if given, wait for the command to end; return (stdout, log).

    A command still running after ``START_SECONDS`` is killed and the test
    fails, so a fault never leaves a serving matchmaker behind.
    """
    if signum is not None:
        process.send_signal(signum)
    try:
        return process.communicate(timeout=START_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()
        process.communicate()
        pytest.fail(f"the command still ran after {START_SECONDS} s and was killed")


def request(port: int, method: str, path: str, body: object = None):
    """Ask the matchmaker over TLS; return (status, JSON or None, shown certificate)."""
    context = ssl.create_default_context()
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    connection = http.client.HTTPSConnection("127.0.0.1", port, context=context)
    data = None if body is None else json.dumps(body)
    connection.request(method, path, data, {"Authorization": "Bearer t"})
    shown = connection.sock.getpeercert(binary_form=True)
    reply = connection.getresponse()
    raw = reply.read()
    connection.close()
    return reply.status, (json.loads(raw) if raw else None), shown


def pin_of(der: bytes) -> str:
    """Return the curl pin of a DER certificate's public key."""
    spki = (
        x509.load_der_x509_certificate(der)
        .public_key()
        .public_bytes(
            serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
        )
    )
    return "sha256//" + base64.b64encode(hashlib.sha256(spki).digest()).decode()


def test_the_command_defaults_to_one_game_on_port_8781() -> None:
    args = _parser().parse_args(["matchmaker"])
    assert (args.port, args.games, args.host) == (DEFAULT_PORT, 1, None)
    assert DEFAULT_PORT == 8781


@pytest.mark.skipif(sys.platform == "win32", reason="ends the process by SIGINT")
def test_the_command_serves_its_games_over_tls_with_the_printed_pin(
    tmp_path: Path,
) -> None:
    settings = tmp_path / "jedimaster" / "settings.json"
    process = start(settings, "--games", "2")
    try:
        lines = announced(process, 2)
        (port,) = map(int, lines["port"])
        (fingerprint,) = lines["fingerprint"]
        games = lines["game"]
        assert len(set(games)) == 2
        assert all(re.fullmatch(GUID_TEXT, game) for game in games)
        for game in games:
            status, listing, shown = request(port, "GET", f"/v1/apps/{game}/rooms")
            assert (status, listing) == (200, {"rooms": []})
            assert pin_of(shown) == fingerprint
        assert request(port, "GET", f"/v1/apps/{STRANGER}/rooms")[:2] == (
            200,
            {"rooms": []},
        )
        assert request(port, "PUT", f"/v1/apps/{STRANGER}/rooms/r", {})[:2] == (
            404,
            {"error": "not_found"},
        )
    finally:
        _, log = ended(process, signal.SIGINT)
    assert process.returncode == 0
    assert (settings.parent / CERT_FILE_NAME).is_file()
    assert (settings.parent / KEY_FILE_NAME).is_file()
    assert "matchmaker serving on port" in log
    for game in games:
        assert game not in log


def test_a_lone_key_file_stops_the_command(tmp_path: Path) -> None:
    settings = tmp_path / "settings.json"
    (tmp_path / KEY_FILE_NAME).write_text("left behind")
    process = start(settings)
    out, log = ended(process)
    assert process.returncode == 2
    assert out == ""
    assert KEY_FILE_NAME in log


@pytest.mark.skipif(sys.platform == "win32", reason="Windows sends no SIGTERM")
def test_sigterm_stops_the_command_even_with_ctrl_c_ignored(tmp_path: Path) -> None:
    # A script's background job starts with SIGINT ignored, so Ctrl-C's
    # signal cannot reach it; SIGTERM must stop it the same clean way.
    process = start(tmp_path / "settings.json", ctrl_c=signal.SIG_IGN)
    try:
        announced(process, 1)
    finally:
        _, log = ended(process, signal.SIGTERM)
    assert process.returncode == 0
    assert "stopped" in log


def test_a_port_in_use_stops_the_command(tmp_path: Path) -> None:
    with socket.socket() as taken:
        taken.bind(("127.0.0.1", 0))
        taken.listen()
        port = taken.getsockname()[1]
        env = {**os.environ, "PYTHONPATH": str(LAUNCHER)}
        result = subprocess.run(
            [sys.executable, "-m", "jedimaster", "matchmaker"]
            + ["--settings", str(tmp_path / "settings.json")]
            + ["--host", "127.0.0.1", "--port", str(port)],
            cwd=LAUNCHER,
            env=env,
            capture_output=True,
            text=True,
            timeout=START_SECONDS,
        )
    assert result.returncode == 2
    assert f"cannot serve on port {port}" in result.stderr
