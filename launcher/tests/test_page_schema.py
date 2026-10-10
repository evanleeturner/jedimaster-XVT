"""The control schema: its file, its examples, its refusals.

Purpose:
    Prove the published schema file equals what the model builds, that
    every example document in ``control-examples/accept`` satisfies it and
    is read the same way by the launcher's request check, that every one in
    ``refuse`` is refused by both, and that the schema closes its objects.

Flow:
    Load the schema file and the example folders; validate each document;
    for a request, run ``parse_request`` on its text too.

Invariants:
    - The TypeScript codec reads the same folders
      (``launcher/page/src/contract.test.ts``).
    - No game data.

Call:
    ``pytest tests/test_page_schema.py``
"""

from __future__ import annotations

import json
import logging

import pytest
from pagedata import EXAMPLES
from pagedata import SCHEMA_FILE
from pagedata import validator

from jedimaster.lists.game import MISSION_TYPES
from jedimaster.page.control import parse_request
from jedimaster.page.control import Refusal
from jedimaster.page.protocol import COMMANDS
from jedimaster.page.schema import control_schema

logger = logging.getLogger(__name__)

ACCEPT = sorted((EXAMPLES / "accept").glob("*.json"))
REFUSE = sorted((EXAMPLES / "refuse").glob("*.json"))


def is_request_shaped(doc) -> bool:
    return isinstance(doc, dict) and "command" in doc


def test_schema_file_matches_model():
    text = json.dumps(control_schema(), indent=2) + "\n"
    assert SCHEMA_FILE.read_text(encoding="utf-8") == text, (
        "schema/control.schema.json is stale: run python tools/gen_schema.py"
    )


def test_every_command_has_an_accepted_example():
    seen = set()
    for path in ACCEPT:
        doc = json.loads(path.read_text(encoding="utf-8"))
        if is_request_shaped(doc):
            seen.add(doc["command"])
    assert seen == set(COMMANDS)


def test_examples_exist_for_replies_and_pushes():
    names = {p.name for p in ACCEPT}
    assert any(n.startswith("reply-ok") for n in names)
    assert any(n.startswith("reply-error") for n in names)
    assert any(n.startswith("push-") for n in names)
    assert len(REFUSE) >= 20


@pytest.mark.parametrize("path", ACCEPT, ids=lambda p: p.name)
def test_accepted_example(path):
    text = path.read_text(encoding="utf-8")
    doc = json.loads(text)
    assert list(validator().iter_errors(doc)) == []
    if is_request_shaped(doc):
        assert not isinstance(parse_request(text), Refusal)


@pytest.mark.parametrize("path", REFUSE, ids=lambda p: p.name)
def test_refused_example(path):
    text = path.read_text(encoding="utf-8")
    doc = json.loads(text)
    assert list(validator().iter_errors(doc)) != []
    if is_request_shaped(doc):
        assert isinstance(parse_request(text), Refusal)


def test_every_object_is_closed():
    defs = control_schema()["$defs"]
    assert all(d["additionalProperties"] is False for d in defs.values())


def test_the_mission_types_in_the_schema_are_the_games_six():
    defs = control_schema()["$defs"]
    assert defs["ShowMissionArgs"]["properties"]["mission_type"]["enum"] == list(
        MISSION_TYPES
    )
    assert defs["ShownMission"]["properties"]["mission_type"]["enum"] == list(
        MISSION_TYPES
    )


def test_the_mission_id_runs_from_zero_to_the_largest_id():
    defs = control_schema()["$defs"]
    for name in ("ShowMissionArgs", "ShownMission"):
        assert defs[name]["properties"]["id"]["minimum"] == 0
        assert defs[name]["properties"]["id"]["maximum"] == 2**31 - 1


def test_hello_promises_revision_3_only():
    defs = control_schema()["$defs"]
    assert defs["HelloResult"]["properties"]["schema_revision"] == {"const": 3}
