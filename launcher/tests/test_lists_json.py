"""JSON export of the lists and the published lists schema.

Purpose:
    Prove that exported synthetic lists of every kind validate against
    ``schema/lists.schema.json``, keep their raw values and lines, that the
    schema file matches the model, and that the schema rejects data it
    should reject.

Flow:
    Read synthetic lists, export, validate with jsonschema (draft 2020-12).

Invariants:
    - No game data.

Call:
    ``pytest tests/test_lists_json.py``
"""

from __future__ import annotations

import copy
import json
import logging
from pathlib import Path

import jsonschema
import pytest
from listdata import crlf

from jedimaster.lists import list_to_json
from jedimaster.lists import read_list
from jedimaster.lists.to_json import lists_schema

logger = logging.getLogger(__name__)

SCHEMA_FILE = Path(__file__).resolve().parents[1] / "schema" / "lists.schema.json"

AWARD = ["1", "main"] + [f"n{i}" for i in range(30)]
SAMPLES = {
    "menu": crlf("//", "[S]", "1", "* 2 3 A.TIE", "T", "4", "& b.tie", "U", "5"),
    "sequence": crlf("1", "a.tie", "Words", end_mark=True),
    "images": crlf("//", "a.bmp one 1", "b.bmp two", end_mark=True),
    "ships": crlf("a.opt 1", "b.opt x"),
    "sounds": crlf("//", "a.wav one", "b.wav"),
    "cutscenes": crlf("2", "// c", "m", "1 0 1", "t", "d", "m2", "1 0", "t2", "d2"),
    "awards": crlf("2", *AWARD, *AWARD[:5]),
}


@pytest.fixture(scope="module")
def validator():
    """Return a draft 2020-12 validator for the published lists schema."""
    schema = json.loads(SCHEMA_FILE.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(schema)
    return jsonschema.Draft202012Validator(schema)


@pytest.mark.parametrize("kind", sorted(SAMPLES))
def test_export_validates_against_schema(validator, kind):
    data = list_to_json(read_list(kind, SAMPLES[kind]))
    assert list(validator.iter_errors(data)) == []
    assert data["kind"] == kind and data["format"] == "jedimaster.lists"
    assert json.loads(json.dumps(data)) == data
    empty = list_to_json(read_list(kind, b""))
    assert list(validator.iter_errors(empty)) == []


def test_raw_values_and_lines_survive():
    menu = list_to_json(read_list("menu", SAMPLES["menu"]))
    entry = menu["items"][2]
    assert entry["kind"] == "entry" and entry["star_numbers"] == [2, 3]
    assert entry["file_line"] == {"line": 4, "text": "* 2 3 A.TIE", "ending": "\r\n"}
    assert menu["items"][4]["title_line"] is None
    images = list_to_json(read_list("images", SAMPLES["images"]))
    assert images["end"] == "bad_number" and images["unread"][0] == {
        "line": 3,
        "text": "b.bmp",
    }


def test_schema_file_matches_model():
    text = json.dumps(lists_schema(), indent=2) + "\n"
    assert SCHEMA_FILE.read_text(encoding="utf-8") == text, (
        "schema/lists.schema.json is stale: run python tools/gen_schema.py"
    )


@pytest.mark.parametrize(
    ("kind", "path", "value"),
    [
        ("menu", ("items", 2, "id"), "1"),
        ("menu", ("items", 0, "kind"), "entry"),
        ("menu", ("items", 2, "complete"), "yes"),
        ("images", ("groups", 0, "extra"), 1),
        ("sequence", ("count_line", "line"), None),
        ("awards", ("records", 0, "multiplayer"), "n0"),
        ("ships", ("format_version",), 2),
    ],
)
def test_schema_rejects_bad_data(validator, kind, path, value):
    data = copy.deepcopy(list_to_json(read_list(kind, SAMPLES[kind])))
    target = data
    for key in path[:-1]:
        target = target[key]
    target[path[-1]] = value
    assert list(validator.iter_errors(data)), path


def test_export_refuses_other_objects():
    with pytest.raises(TypeError):
        list_to_json({"kind": "menu"})
    with pytest.raises(ValueError):
        read_list("palette", b"")
