"""The stand-in AI program: no computer craft decides anything.

Purpose:
    Answer the engine's questions about computer craft with no action for any
    craft, so a flight runs through the engine's AI pipe before any behavior
    is written. The bytes are the ones ``engine/src/xvt_runtime/ai/ai_wire.h``
    sets out; each craft's notebook goes back unchanged.

Flow:
    1. read one question from standard input: a u32 length, then that many
       bytes
    2. check its header (magic, version, kind, the zero) and that its length
       is the one its craft count needs
    3. write the answer, the question's tick and craft count, then each
       craft's object index, its notebook and no action, and flush it
    4. at the end of standard input, the engine's sign to stop, exit 0

Invariants:
    - The answer depends only on the question's bytes.
    - Standard output carries answers only; log lines go to standard error.
    - A question that breaks the format ends the program with exit code 2
      after one ERROR line; it is never answered.

Call:
    ``python3 ai/jedimaster_ai``; the engine starts it from a launch
    description's ``ai_program``.
"""

from __future__ import annotations

import logging
import struct
import sys
from typing import BinaryIO

logger = logging.getLogger("jedimaster_ai")

MAGIC = 0x57504D4A  # "JMPW"
VERSION = 1
KIND_QUESTION = 1
KIND_ANSWER = 2
NOTEBOOK_BYTES = 64
# A craft view: 38 bytes of fields, the object index first, then the notebook.
NOTEBOOK_OFFSET = 38
CRAFT_VIEW_BYTES = NOTEBOOK_OFFSET + NOTEBOOK_BYTES
LENGTH = struct.Struct("<I")
# After the length field: magic, version, kind, tick, craft count, zero.
HEADER = struct.Struct("<IHHIHH")


class QuestionError(ValueError):
    """A question that breaks the byte format."""


def answer_for(question: bytes) -> bytes:
    """Return the answer, its length field included, to a question's bytes.

    ``question`` holds the bytes after the question's length field. Raises
    QuestionError when its header or its length is not a question's.
    """
    if len(question) < HEADER.size:
        raise QuestionError(
            f"a question of {len(question)} bytes is shorter than its header"
        )
    magic, version, kind, tick, count, zero = HEADER.unpack_from(question)
    if (magic, version, kind, zero) != (MAGIC, VERSION, KIND_QUESTION, 0):
        raise QuestionError(
            f"the header magic={magic:#x} version={version} kind={kind} "
            f"zero={zero} is not a version {VERSION} question's"
        )
    expected = HEADER.size + count * CRAFT_VIEW_BYTES
    if len(question) != expected:
        raise QuestionError(
            f"a question about {count} craft holds {len(question)} bytes, "
            f"not {expected}"
        )
    body = bytearray(HEADER.pack(MAGIC, VERSION, KIND_ANSWER, tick, count, 0))
    for craft in range(count):
        view = HEADER.size + craft * CRAFT_VIEW_BYTES
        body += question[view : view + 2]  # the object index
        body += question[view + NOTEBOOK_OFFSET : view + CRAFT_VIEW_BYTES]
        body.append(0)  # no action
    logger.debug("answered tick=%d craft=%d", tick, count)
    return LENGTH.pack(len(body)) + bytes(body)


def _read_question(stream: BinaryIO) -> bytes | None:
    """Return the next question's bytes after its length field.

    Returns None at the end of the stream before a question starts. Raises
    QuestionError when the stream ends partway through one.
    """
    head = stream.read(LENGTH.size)
    if not head:
        return None
    if len(head) < LENGTH.size:
        raise QuestionError(f"the stream ended {len(head)} bytes into a length")
    (length,) = LENGTH.unpack(head)
    question = stream.read(length)
    if len(question) < length:
        raise QuestionError(
            f"the stream ended {len(question)} bytes into a {length}-byte question"
        )
    return question


def main() -> int:
    """Answer questions until standard input ends; return the exit code."""
    logging.basicConfig(
        stream=sys.stderr,
        level=logging.INFO,
        format="%(asctime)s %(levelname).1s %(name)s %(message)s",
    )
    logger.info("the stand-in started: no computer craft decides anything")
    answered = 0
    try:
        while (question := _read_question(sys.stdin.buffer)) is not None:
            sys.stdout.buffer.write(answer_for(question))
            sys.stdout.buffer.flush()
            answered += 1
    except QuestionError as exc:
        logger.error("question refused, the stand-in stops: %s", exc)
        return 2
    logger.info("the stand-in stopped after %d questions", answered)
    return 0


if __name__ == "__main__":
    sys.exit(main())
