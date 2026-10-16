"""Fences for the matchmaker's certificate: made once, never replaced, pinned.

The seam fence is the last test: the real certificate library makes the
pair, the real ``ssl`` module serves it on a socket, and the fingerprint is
recomputed from the certificate the client was shown.
"""

from __future__ import annotations

import asyncio
import base64
import hashlib
import re
import ssl
import sys
from pathlib import Path

import pytest
from cryptography import x509
from cryptography.hazmat.primitives import serialization

from jedimaster.matchmaker.certificate import CERT_FILE_NAME
from jedimaster.matchmaker.certificate import CertificateError
from jedimaster.matchmaker.certificate import ensure_certificate
from jedimaster.matchmaker.certificate import KEY_FILE_NAME
from jedimaster.matchmaker.certificate import tls_context

PIN = re.compile(r"sha256//[A-Za-z0-9+/]{43}=")


def test_a_pair_is_made_once_and_then_loaded_unchanged(tmp_path: Path) -> None:
    folder = tmp_path / "settings"
    first = ensure_certificate(folder)
    assert (first.cert_path, first.key_path) == (
        folder / CERT_FILE_NAME,
        folder / KEY_FILE_NAME,
    )
    assert PIN.fullmatch(first.fingerprint)
    files = first.cert_path.read_bytes(), first.key_path.read_bytes()
    again = ensure_certificate(folder)
    assert again == first
    assert (again.cert_path.read_bytes(), again.key_path.read_bytes()) == files
    assert sorted(path.name for path in folder.iterdir()) == sorted(
        [CERT_FILE_NAME, KEY_FILE_NAME]
    )


def test_two_installs_get_two_fingerprints(tmp_path: Path) -> None:
    assert (
        ensure_certificate(tmp_path / "a").fingerprint
        != ensure_certificate(tmp_path / "b").fingerprint
    )


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX file modes")
def test_only_the_owner_may_read_the_key(tmp_path: Path) -> None:
    certificate = ensure_certificate(tmp_path)
    assert certificate.key_path.stat().st_mode & 0o777 == 0o600


@pytest.mark.parametrize("kept", [CERT_FILE_NAME, KEY_FILE_NAME])
def test_a_lone_file_is_refused_and_left_as_it_is(tmp_path: Path, kept: str) -> None:
    made = ensure_certificate(tmp_path / "made")
    folder = tmp_path / "lone"
    folder.mkdir()
    (folder / kept).write_bytes((made.cert_path.parent / kept).read_bytes())
    before = (folder / kept).read_bytes()
    with pytest.raises(CertificateError) as caught:
        ensure_certificate(folder)
    assert kept in str(caught.value)
    assert [path.name for path in folder.iterdir()] == [kept]
    assert (folder / kept).read_bytes() == before


def test_a_pair_that_does_not_read_is_refused(tmp_path: Path) -> None:
    certificate = ensure_certificate(tmp_path)
    key = certificate.key_path.read_bytes()
    certificate.cert_path.write_bytes(b"not a certificate")
    with pytest.raises(CertificateError, match=CERT_FILE_NAME):
        ensure_certificate(tmp_path)
    ensure_certificate(tmp_path / "other")
    certificate.cert_path.write_bytes(
        (tmp_path / "other" / CERT_FILE_NAME).read_bytes()
    )
    certificate.key_path.write_bytes(b"not a key")
    with pytest.raises(CertificateError, match=KEY_FILE_NAME):
        ensure_certificate(tmp_path)
    certificate.key_path.write_bytes(key)
    with pytest.raises(CertificateError, match="is not the key of"):
        ensure_certificate(tmp_path)


def test_the_server_takes_tls_1_2_or_newer(tmp_path: Path) -> None:
    context = tls_context(ensure_certificate(tmp_path))
    assert context.minimum_version == ssl.TLSVersion.TLSv1_2


async def test_the_fingerprint_is_the_public_key_the_server_shows(
    tmp_path: Path,
) -> None:
    certificate = ensure_certificate(tmp_path)
    server = await asyncio.start_server(
        lambda reader, writer: writer.close(),
        "127.0.0.1",
        0,
        ssl=tls_context(certificate),
    )
    port = server.sockets[0].getsockname()[1]
    client = ssl.create_default_context()
    client.check_hostname = False
    client.verify_mode = ssl.CERT_NONE
    _, writer = await asyncio.open_connection("127.0.0.1", port, ssl=client)
    shown = writer.get_extra_info("ssl_object").getpeercert(binary_form=True)
    writer.close()
    server.close()
    await server.wait_closed()
    spki = (
        x509.load_der_x509_certificate(shown)
        .public_key()
        .public_bytes(
            serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
        )
    )
    pin = "sha256//" + base64.b64encode(hashlib.sha256(spki).digest()).decode()
    assert pin == certificate.fingerprint
