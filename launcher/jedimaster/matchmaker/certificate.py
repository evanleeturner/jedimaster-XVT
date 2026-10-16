"""The matchmaker's certificate: made once per install, known by its fingerprint.

Purpose:
    The matchmaker serves HTTPS with a certificate no public authority
    signed, so no game can check it against the system's store. It is made
    once, kept beside the bot's token, and recognized by the fingerprint of
    its public key, which the friends' launchers and the games are given to
    compare with what the server shows.

Flow:
    ``ensure_certificate`` loads the pair in a folder, or makes one when
    neither file is there: an EC P-256 key and a self-signed certificate
    valid for 10 years. ``fingerprint`` hashes a certificate's public key;
    ``tls_context`` is the server side's TLS settings for the pair.

Invariants:
    - The key file is made with mode 0600 on POSIX, as the token file is;
      on Windows it sits in the user's own profile folder.
    - A pair is never replaced: one file without the other, a file that does
      not read, or a key that is not the certificate's is refused with the
      file's name, since a new pair would change the fingerprint every
      linked friend holds.
    - The fingerprint is curl's pinned public key form: ``sha256//`` and the
      base64 of the SHA-256 of the key's DER SubjectPublicKeyInfo.
    - Each file is written to a temporary name and moved into place, the key
      first, so a reader never sees half a file.
    - The server accepts TLS 1.2 or newer.

Call:
    ``certificate = ensure_certificate(token_path(settings_path).parent)``
"""

from __future__ import annotations

import base64
import datetime
import hashlib
import logging
import os
import ssl
from dataclasses import dataclass
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

logger = logging.getLogger(__name__)

CERT_FILE_NAME = "matchmaker_cert.pem"
KEY_FILE_NAME = "matchmaker_key.pem"
KEY_FILE_MODE = 0o600
CERT_FILE_MODE = 0o644
COMMON_NAME = "jedimaster matchmaker"
VALID_DAYS = 3650
PIN_PREFIX = "sha256//"


class CertificateError(Exception):
    """A certificate pair that cannot be used, said with the file's name."""


@dataclass(frozen=True)
class Certificate:
    """The pair's two files and the certificate's public key fingerprint."""

    cert_path: Path
    key_path: Path
    fingerprint: str


def fingerprint(cert: x509.Certificate) -> str:
    """Return ``cert``'s public key fingerprint in curl's pinned form."""
    spki = cert.public_key().public_bytes(
        serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
    )
    return PIN_PREFIX + base64.b64encode(hashlib.sha256(spki).digest()).decode("ascii")


def write_private(path: Path, data: bytes, mode: int) -> None:
    """Write ``data`` to ``path`` through a temporary file made with ``mode``."""
    temp = path.with_name(path.name + ".tmp")
    temp.unlink(missing_ok=True)
    fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(data)
        os.replace(temp, path)
    except BaseException:
        temp.unlink(missing_ok=True)
        raise


def make_pair(cert_path: Path, key_path: Path) -> x509.Certificate:
    """Make a key and its self-signed certificate; write both; return the certificate."""
    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, COMMON_NAME)])
    now = datetime.datetime.now(datetime.UTC)
    cert = (
        x509.CertificateBuilder()
        .subject_name(name)
        .issuer_name(name)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(minutes=5))
        .not_valid_after(now + datetime.timedelta(days=VALID_DAYS))
        .sign(key, hashes.SHA256())
    )
    key_pem = key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    )
    cert_path.parent.mkdir(parents=True, exist_ok=True)
    write_private(key_path, key_pem, KEY_FILE_MODE)
    write_private(
        cert_path, cert.public_bytes(serialization.Encoding.PEM), CERT_FILE_MODE
    )
    return cert


def load_pair(cert_path: Path, key_path: Path) -> x509.Certificate:
    """Return the certificate of a pair whose key is its own; refuse anything else."""
    try:
        cert = x509.load_pem_x509_certificate(cert_path.read_bytes())
    except (OSError, ValueError) as err:
        raise CertificateError(f"{cert_path} is not a certificate: {err}") from err
    try:
        key = serialization.load_pem_private_key(key_path.read_bytes(), password=None)
    except (OSError, ValueError, TypeError) as err:
        raise CertificateError(f"{key_path} is not a private key: {err}") from err
    public = serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
    if key.public_key().public_bytes(*public) != cert.public_key().public_bytes(
        *public
    ):
        raise CertificateError(f"{key_path} is not the key of {cert_path}")
    return cert


def ensure_certificate(folder: Path) -> Certificate:
    """Return the pair in ``folder``, made there first when neither file exists.

    Raises ``CertificateError`` when only one file exists or the pair does
    not read (the message names the file), and ``OSError`` when a new pair
    cannot be written.
    """
    cert_path = folder / CERT_FILE_NAME
    key_path = folder / KEY_FILE_NAME
    have_cert, have_key = cert_path.exists(), key_path.exists()
    if have_cert != have_key:
        lone, missing = (cert_path, key_path) if have_cert else (key_path, cert_path)
        raise CertificateError(
            f"{lone} has no partner {missing.name}: move it away to make a new"
            " certificate (every linked friend then needs a new link)"
        )
    if have_cert:
        cert = load_pair(cert_path, key_path)
        logger.debug("certificate loaded from %s", cert_path)
    else:
        cert = make_pair(cert_path, key_path)
        logger.info("certificate made at %s", cert_path)
    result = Certificate(cert_path, key_path, fingerprint(cert))
    logger.debug("certificate fingerprint=%s", result.fingerprint)
    return result


def tls_context(certificate: Certificate) -> ssl.SSLContext:
    """Return the server's TLS settings: the pair, TLS 1.2 or newer."""
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(certificate.cert_path, certificate.key_path)
    return context
