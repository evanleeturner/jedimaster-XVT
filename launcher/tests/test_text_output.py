"""The text outputs: the view of an install, the sheets' form, JSON, schema, CLI.

Purpose:
    Prove the text view resolves each file Balance of Power first (or not)
    in any letter case and keeps a refused ``strings.txt`` as its refusal;
    the renderers print the answer sheets' form line for line; the JSON
    export keeps every byte (Latin-1), records the refusal as an error,
    and validates against the published schema, which matches its model;
    and the ``text`` command line dumps and exports with the package's
    exit statuses.

Flow:
    Build a fake install with ``textdata``; view, render, export, read back.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_text_output.py``
"""

from __future__ import annotations

import copy
import json
import logging
from pathlib import Path

import jsonschema
import pytest
from textdata import crlf
from textdata import strings_bytes
from textdata import write_files

from jedimaster.__main__ import main
from jedimaster.text import read_credits
from jedimaster.text import read_errors
from jedimaster.text import read_front
from jedimaster.text import read_joystick
from jedimaster.text import read_specs
from jedimaster.text import read_strings
from jedimaster.text import render_text
from jedimaster.text import SpecEntry
from jedimaster.text import SpecsFile
from jedimaster.text import text_schema
from jedimaster.text import text_to_json
from jedimaster.text import text_view
from jedimaster.text.tables import GENDER_TABLES
from jedimaster.text.tables import MESSAGES
from jedimaster.text.tables import MODELS
from jedimaster.text.tables import TABLES

logger = logging.getLogger(__name__)

SCHEMA_FILE = Path(__file__).resolve().parents[1] / "schema" / "text.schema.json"
MODEL = next(t for t in TABLES if t.kind == MODELS)


@pytest.fixture(scope="module")
def validator():
    """Return a draft 2020-12 validator for the published schema file."""
    schema = json.loads(SCHEMA_FILE.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(schema)
    return jsonschema.Draft202012Validator(schema)


def make_install(root: Path) -> Path:
    """Write an install: base files (a refused strings.txt) and BoP's."""
    return write_files(
        root,
        {
            "STRINGS.TXT": strings_bytes(
                models={4: b"Xbad"}, lines={(MESSAGES, 3): b"made-up \xe9"}
            ),
            "FRONTTXT.TXT": crlf(b"base menu"),
            "SPECDESC.TXT": crlf(b"base craft"),
            "XVTERR.TXT": crlf(b"Error\\nhere"),
            "JOYSTICK.TXT": crlf(b"1 A base"),
            "CREDITS.TXT": crlf(b"11 12 1 13 14 0 15 16 17 18", b"base credit"),
            "BalanceOfPower/strings.txt": strings_bytes(models={0: b"fQqueen"}),
            "BalanceOfPower/FrontTxt.TXT": crlf(b"bop menu", b"caf\xe9"),
            "BalanceOfPower/specdesc.txt": crlf(b"bop craft"),
            "BalanceOfPower/joystick.txt": crlf(b"2 B bop"),
            "BalanceOfPower/credits.txt": crlf(b"bop credit"),
        },
    )


def test_view_reads_balance_of_power_first_in_any_case(tmp_path):
    root = make_install(tmp_path / "XvT")
    view = text_view(root)
    files = view.files
    assert list(files) == ["strings", "front", "specs", "errors", "joystick", "credits"]
    assert files["front"].result.entries == [b"bop menu", b"caf\xe9"]
    assert files["front"].file == "BalanceOfPower/fronttxt.txt"
    assert files["errors"].file == "xvterr.txt"
    assert files["errors"].result.messages == [b"Error\nhere\n"]
    assert files["strings"].error is None
    base = text_view(root, balance_of_power=False)
    assert base.files["front"].result.entries == [b"base menu"]
    assert base.files["strings"].result is None
    assert base.files["strings"].error.table == MODEL.name


def test_a_missing_file_is_kept_as_an_error(tmp_path, caplog):
    caplog.set_level(logging.WARNING)
    root = write_files(tmp_path / "XvT", {"fronttxt.txt": b"x\n"})
    view = text_view(root)
    assert view.files["front"].result.entries == [b"x"]
    assert isinstance(view.files["credits"].error, FileNotFoundError)
    assert "credits.txt not found" in caplog.text


def sheet(kind: str, result) -> list[str]:
    return render_text(kind, result, "name.txt", "dir/name.txt").splitlines()


def test_render_header_and_front():
    lines = sheet("front", read_front(b"a\n\tb\xff\n"))
    assert lines == [
        "kind front",
        'name "name.txt"',
        'file "dir/name.txt"',
        "count 2",
        'entry 0 "a"',
        'entry 1 "\\tb\\xff"',
    ]


def test_render_strings_tables_and_a_refusal():
    strings = read_strings(strings_bytes(models={1: b"fXshe"}))
    lines = sheet("strings", strings)
    first = TABLES[0]
    assert lines[3] == f"table {first.name} {first.length}"
    assert lines[4] == f'entry 0 "{first.name} 0"'
    assert 'entry 1 gender=1 "she"' in lines
    assert any(line.startswith("entry 187.") for line in lines)
    feminine, neutered = (t.name for _, t in GENDER_TABLES)
    assert f"table {neutered} absent" == lines[-1]
    assert any(line.startswith(f"table {feminine} ") for line in lines)
    assert sheet("strings", None) == [
        "kind strings",
        'name "name.txt"',
        'file "dir/name.txt"',
    ]


def test_render_specs_errors_joystick():
    specs = sheet("specs", read_specs(crlf(b"N" * 70, b"m", b"u", b"d", b"c")))
    assert specs[3] == "result 1"
    assert specs[4] == (
        f'spec 0 name="{"N" * 64}" manufacturer="m" users="u" description="d" crew="c"'
    )
    assert (
        specs[-1] == 'spec 92 name="" manufacturer="" users="" description="" crew=""'
    )
    full = SpecsFile([SpecEntry(name=b"x" * 70, crew=b"y" * 65)] * 93, 93)
    assert sheet("specs", full)[4].startswith(f'spec 0 name="{"x" * 64}" manu')
    assert sheet("specs", full)[4].endswith(f'crew="{"y" * 64}"')
    errors = sheet("errors", read_errors(b"a\\nb\r\n"))
    assert errors[3:] == ['entry 0 "a\\nb\\n"', "count 1"]
    joystick = sheet("joystick", read_joystick(b"300 " + b"n" * 25 + b" " + b"d" * 90))
    assert joystick[3:5] == ["result 1", "count 1"]
    assert joystick[5] == f'action 0 code=44 name="{"n" * 20}" description="{"d" * 90}"'


def test_render_credits_pages():
    data = crlf(b"1 2 2 3 4 0 5 6 7 8", b"~255 255 0 yellow", b"*", b"9 8 x")
    lines = sheet("credits", read_credits(data))
    assert lines[3] == (
        "page 0 result=1 buffer=1 more=1 duration=4 fade=3 text_x=1 "
        "text_y=2 logo=0 logo_x=5 logo_y=6"
    )
    assert lines[4] == 'line 0 color=ffe0 "yellow"'
    assert lines[5] == 'line 1 color=0000 ""'
    page_1 = lines.index(
        "page 1 result=1 buffer=1 more=0 duration=unread fade=unread "
        "text_x=9 text_y=8 logo=unread logo_x=unread logo_y=unread"
    )
    assert lines[page_1 + 1] == 'line 0 color=ffe0 "x"'
    assert len(lines) == 3 + 2 * 33
    unread = sheet("credits", read_credits(b""))
    assert unread[3] == "page 0 result=1 buffer=1 more=0 header=unread"


@pytest.fixture(scope="module")
def exported(tmp_path_factory) -> dict:
    """Return the export of one fake install, each table cut to 2 entries.

    The cut keeps the document valid and the validation quick.
    """
    data = export(make_install(tmp_path_factory.mktemp("text") / "XvT"))
    for table in data["strings"]["tables"]:
        del table["entries"][2:]
    return data


def export(root: Path, balance_of_power: bool = True) -> dict:
    data = text_to_json(text_view(root, balance_of_power))
    return json.loads(json.dumps(data, ensure_ascii=False))


@pytest.mark.parametrize("balance_of_power", [True, False])
def test_export_validates_against_schema(tmp_path, validator, balance_of_power):
    data = export(make_install(tmp_path / "XvT"), balance_of_power)
    assert list(validator.iter_errors(data)) == []


def test_export_keeps_bytes_and_records_the_refusal(tmp_path):
    root = make_install(tmp_path / "XvT")
    data = export(root)
    assert data["front"]["entries"][1].encode("latin-1") == b"caf\xe9"
    assert data["front"]["no_text"] == "No text."
    tables = data["strings"]["tables"]
    assert tables[0]["entries"][0]["line"] == 1
    base = export(root, balance_of_power=False)
    error = base["strings"]["error"]
    assert (error["table"], error["line"]) == (MODEL.name, 5)
    assert error["stop"] == "made-up \xe9" and error["game_stop"] is True
    assert error["message"].startswith("strings.txt: table ")
    assert base["strings"]["tables"] is None
    assert base["credits"]["pages"][0]["header"]["paragraph"] == 1
    assert base["credits"]["pages"][0]["header"]["spare_2"] == 18
    assert base["credits"]["pages"][0]["header_read"] == 10
    assert data["credits"]["pages"][0]["header_read"] == 0
    assert data["credits"]["pages"][0]["header"]["text_x"] is None
    specs = base["specs"]
    assert (specs["complete"], specs["cut"]) == (0, 0)
    assert specs["fields"][3] == {"name": "description", "size": 256}
    assert specs["entries"][0]["name"] == "base craft"
    assert base["joystick"]["actions"][0]["code"] == 1


def test_schema_file_matches_model():
    text = json.dumps(text_schema(), indent=2) + "\n"
    assert SCHEMA_FILE.read_text(encoding="utf-8") == text, (
        "schema/text.schema.json is stale: run python tools/gen_schema.py"
    )


@pytest.mark.parametrize(
    ("path", "value"),
    [
        (("front", "entries", 0), 5),
        (("joystick", "actions", 0, "code"), 256),
        (("credits", "pages", 0, "buffer"), 2),
        (("credits", "pages", 0, "lines"), []),
        (("strings", "tables", 0, "kind"), "other"),
        (("strings", "tables", 0, "entries", 0, "gender"), 3),
        (("specs", "entries"), []),
        (("errors", "extra"), 1),
        (("front", "name"), "other.txt"),
    ],
    ids=[
        "front-entry",
        "joystick-code",
        "credits-buffer",
        "credits-lines",
        "strings-kind",
        "strings-gender",
        "specs-count",
        "errors-extra",
        "front-name",
    ],
)
def test_schema_rejects_bad_data(exported, validator, path, value):
    data = copy.deepcopy(exported)
    target = data
    for key in path[:-1]:
        target = target[key]
    target[path[-1]] = value
    assert list(validator.iter_errors(data)), path


def test_text_dump_by_path_and_game_path(tmp_path, capsys):
    root = make_install(tmp_path / "XvT")
    assert main(["text", "dump", "front", str(root / "FRONTTXT.TXT")]) == 0
    out = capsys.readouterr().out
    assert out.splitlines()[3:] == ["count 1", 'entry 0 "base menu"']
    assert (
        main(["text", "dump", "joystick", "joystick.txt", "--install", str(root)]) == 0
    )
    assert 'name="B"' in capsys.readouterr().out


def test_text_dump_statuses(tmp_path, capsys):
    root = make_install(tmp_path / "XvT")
    assert main(["text", "dump", "strings", str(root / "STRINGS.TXT")]) == 1
    assert capsys.readouterr().out.splitlines()[0] == "kind strings"
    assert main(["text", "dump", "front", "none.txt", "--install", str(root)]) == 2
    folder = tmp_path / "folder.txt"
    folder.mkdir()
    assert main(["text", "dump", "front", str(folder), "--install", str(root)]) == 2


def test_text_export(tmp_path, capsys, validator):
    root = make_install(tmp_path / "XvT")
    out = tmp_path / "out"
    assert main(["text", "export", str(root), str(out), "--no-balance-of-power"]) == 0
    assert "exported 5 of 6 text files" in capsys.readouterr().out
    data = json.loads((out / "text.json").read_text(encoding="utf-8"))
    assert data["balance_of_power"] is False and data["strings"]["error"]
    assert list(validator.iter_errors(data)) == []
    assert main(["text", "export", str(tmp_path / "none"), str(out)]) == 2
    blocked = tmp_path / "file"
    blocked.write_bytes(b"")
    assert main(["text", "export", str(root), str(blocked)]) == 1
