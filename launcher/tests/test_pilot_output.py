"""The pilot's JSON, its schema and the ``pilot`` command line.

Purpose:
    Prove the export keeps the game program's member names as keys, text
    fields as their text and byte fields as hex, validates against the
    published schema (which matches its model and rejects what it should),
    and that ``pilot dump``, ``pilot load`` and ``pilot export`` print or
    write what they should with the right exit status.

Flow:
    Build records with ``pilotdata`` and a fake install, load, export,
    validate; run the command line on files in a temporary folder.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_pilot_output.py``
"""

from __future__ import annotations

import copy
import json
import logging
from pathlib import Path

import jsonschema
import pytest
from pilotdata import install
from pilotdata import record

from jedimaster.__main__ import main
from jedimaster.pilot import BASE_RECORD
from jedimaster.pilot import Defaults
from jedimaster.pilot import FULL_RECORD
from jedimaster.pilot import load_files
from jedimaster.pilot import pilot_schema
from jedimaster.pilot import pilot_to_json
from jedimaster.pilot import struct

logger = logging.getLogger(__name__)

SCHEMA_FILE = Path(__file__).resolve().parents[1] / "schema" / "pilot.schema.json"
DEFAULTS = Defaults(2, b"Rookie", b" plays", (1, 2, 3))


@pytest.fixture(scope="module")
def exported() -> dict:
    """Return the JSON of a load of both files with a few values set."""
    pl2 = record(FULL_RECORD, {("multiplayer_game_name",): b"G\xe9\0rest"})
    plt = record(
        BASE_RECORD,
        {
            ("name",): b"Ace",
            ("rating",): 6,
            ("faction_statistics", 1, "field_bc"): b"\x0a\xff",
            ("teams", 1, "kills"): -4,
        },
    )
    return pilot_to_json(load_files("Ace0.plt", bytes(plt), bytes(pl2), DEFAULTS), True)


@pytest.fixture(scope="module")
def schema() -> dict:
    """Return the published schema file, checked as a draft 2020-12 schema."""
    data = json.loads(SCHEMA_FILE.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(data)
    return data


def part(schema: dict, *keys: str) -> jsonschema.Draft202012Validator:
    """Return a validator for one part of the schema, its definitions kept."""
    target = schema
    for key in keys:
        target = target[key]
    return jsonschema.Draft202012Validator({**target, "$defs": schema["$defs"]})


def test_export_keeps_names_text_and_hex(exported):
    assert exported["format"] == "jedimaster.pilot" and exported["result"] == 1
    assert exported["files"] == {"pl2": True, "plt": True}
    assert exported["file"] == "Ace0.plt" and exported["balance_of_power"] is True
    rec = exported["record"]
    assert list(rec) == [m.name for m in struct(FULL_RECORD).members]
    assert rec["name"] == "Ace" and rec["rating"] == 6
    assert rec["multiplayer_game_name"] == "G\xe9"
    side = rec["faction_statistics"][1]
    assert side["field_bc"] == "0aff" + "00" * (len(side["field_bc"]) // 2 - 2)
    assert rec["teams"][1]["kills"] == -4
    assert rec["teams"][0]["kills"] == 0
    assert len(rec["main_stats"]["kills_per_craft_per_mt"][0]) > 1
    assert json.loads(json.dumps(exported)) == exported


def test_export_validates_against_schema(schema, exported):
    validator = jsonschema.Draft202012Validator(schema)
    assert list(validator.iter_errors(exported)) == []


def test_schema_file_matches_model():
    text = json.dumps(pilot_schema(), indent=2) + "\n"
    assert SCHEMA_FILE.read_text(encoding="utf-8") == text, (
        "schema/pilot.schema.json is stale: run python tools/gen_schema.py"
    )


MEMBER = ("$defs", FULL_RECORD, "properties")


def payload_size() -> int:
    """Return the bytes of the full record's payload field."""
    return next(
        m.size for m in struct(FULL_RECORD).members if m.name == "xvt_record_payload"
    )


@pytest.mark.parametrize(
    ("keys", "good", "bad"),
    [
        ((*MEMBER, "rating"), 2**31 - 1, 2**31),
        ((*MEMBER, "rating"), -(2**31), -(2**31) - 1),
        ((*MEMBER, "num_human_players_last_mission"), 2**32 - 1, -1),
        ((*MEMBER, "name"), "x" * 14, "x" * 15),
        ((*MEMBER, "xvt_record_payload"), "0a", "zz"),
        ((*MEMBER, "xvt_record_payload"), "0a", "0a0"),
        ((*MEMBER, "mission_description_ids"), [0] * 6, [0] * 5),
        (("properties", "result"), 0, 2),
    ],
)
def test_schema_rejects_bad_values(schema, keys, good, bad):
    validator = part(schema, *keys)
    if keys[-1] == "xvt_record_payload":
        good, bad = good * payload_size(), bad * payload_size()
    assert list(validator.iter_errors(good)) == []
    assert list(validator.iter_errors(bad)), keys


def test_schema_rejects_unknown_and_missing_members(schema, exported):
    validator = part(schema, "$defs", "pilot_team")
    team = copy.deepcopy(exported["record"]["teams"][1])
    assert list(validator.iter_errors(team)) == []
    assert list(validator.iter_errors({**team, "extra": 1}))
    del team["kills"]
    assert list(validator.iter_errors(team))
    top = copy.deepcopy(exported)
    top["record"] = {}
    loose = {**schema, "properties": {**schema["properties"], "record": {}}}
    checker = jsonschema.Draft202012Validator(loose)
    assert list(checker.iter_errors(top)) == []
    assert list(checker.iter_errors({**top, "format": "other"}))


def write_pilot(folder: Path, plt: bool = True, pl2: bool = True) -> None:
    """Write the pilot ``Ace0`` into ``folder``: its .plt and .pl2 as asked."""
    folder.mkdir(parents=True, exist_ok=True)
    if plt:
        data = record(BASE_RECORD, {("name",): b"Ace", ("rating",): 6})
        (folder / "Ace0.plt").write_bytes(bytes(data))
    if pl2:
        (folder / "Ace0.pl2").write_bytes(bytes(record(FULL_RECORD, {("rating",): 3})))


def test_pilot_dump(tmp_path, capsys):
    write_pilot(tmp_path)
    assert main(["pilot", "dump", str(tmp_path / "Ace0.plt")]) == 0
    out = capsys.readouterr().out.splitlines()
    assert out[:2] == [
        "kind raw",
        f"bytes {struct(BASE_RECORD).size} of {struct(BASE_RECORD).size}",
    ]
    assert "rating = 6" in out
    assert main(["pilot", "dump", str(tmp_path / "Ace0.pl2")]) == 0
    assert "rating = 3" in capsys.readouterr().out.splitlines()
    (tmp_path / "Bad0.plt").write_bytes(b"short")
    assert main(["pilot", "dump", str(tmp_path / "Bad0.plt")]) == 1
    assert main(["pilot", "dump", str(tmp_path / "none.plt")]) == 2
    assert capsys.readouterr().out == ""


def test_pilot_load(tmp_path, capsys):
    root = install(tmp_path / "game", bop_texts={470: b" bop"})
    write_pilot(tmp_path / "both")
    assert main(["pilot", "load", str(tmp_path / "both"), "Ace0.plt", str(root)]) == 0
    lines = capsys.readouterr().out.splitlines()
    assert lines[:3] == ["kind load", "files pl2=1 plt=1", "result 1"]
    assert "rating = 6" in lines
    write_pilot(tmp_path / "plt", pl2=False)
    for flags, suffix in (([], "Ace bop"), (["--no-balance-of-power"], "Aceentry 470")):
        argv = ["pilot", "load", str(tmp_path / "plt"), "Ace0", str(root), *flags]
        assert main(argv) == 0
        out = capsys.readouterr().out
        assert f'multiplayer_game_name = "{suffix}"' in out
    assert main(["pilot", "load", str(tmp_path / "x"), "Ace0", str(root)]) == 2
    assert main(["pilot", "load", str(tmp_path / "plt"), "Ace0", str(tmp_path)]) == 2
    (root / "BalanceOfPower" / "fronttxt.txt").unlink()
    (root / "fronttxt.txt").unlink()
    assert main(["pilot", "load", str(tmp_path / "plt"), "Ace0", str(root)]) == 1
    assert capsys.readouterr().out == ""


def test_pilot_export(tmp_path, capsys):
    root = install(tmp_path / "game")
    write_pilot(tmp_path / "pilot", pl2=False)
    out = tmp_path / "out"
    argv = ["pilot", "export", str(tmp_path / "pilot"), "Ace0.plt", str(root), str(out)]
    assert main(argv) == 0
    assert "pilot.json" in capsys.readouterr().out
    data = json.loads((out / "pilot.json").read_text(encoding="utf-8"))
    assert data["format"] == "jedimaster.pilot" and data["result"] == 1
    assert data["files"] == {"pl2": False, "plt": True}
    assert data["record"]["mission_description_ids"][0] == 21
    (tmp_path / "file").write_text("x")
    blocked = [
        "pilot",
        "export",
        str(tmp_path / "pilot"),
        "Ace0",
        str(root),
        str(tmp_path / "file"),
    ]
    assert main(blocked) == 1
