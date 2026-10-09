"""The models' export: models.json and its schema, and the ``models`` command.

Purpose:
    Prove ``models.json`` describes every model name (file, version,
    counts, ``.glb`` files, textures and their glow), every object type's
    and craft type's model, validates against the published schema (which
    matches its model and rejects bad data), and that ``models dump`` and
    ``models export`` print and write what they should, with the exit
    statuses they promise.

Flow:
    Build models with ``optbuilder`` and installs with ``textdata``;
    export; validate with jsonschema (draft 2020-12); run the command
    line; read the files back.

Invariants:
    - No game data: the object tables are replaced by synthetic ones.

Call:
    ``pytest tests/test_models_export.py``
"""

from __future__ import annotations

import copy
import json
import logging
from pathlib import Path

import jsonschema
import pytest
from optbuilder import build_opt
from optbuilder import Face
from optbuilder import faces
from optbuilder import N
from optbuilder import palette
from optbuilder import Tex
from optbuilder import vecs
from test_models_gltf import glow_color
from test_models_gltf import read_glb
from textdata import write_files

from jedimaster.__main__ import main
from jedimaster.models import models_schema
from jedimaster.models import models_to_json
from jedimaster.models import models_view
from jedimaster.models import objects as objects_module
from jedimaster.models import objects_view
from jedimaster.models import ObjectType
from jedimaster.models import to_json as to_json_module
from jedimaster.models.to_json import glb_stem

logger = logging.getLogger(__name__)

SCHEMA_FILE = Path(__file__).resolve().parents[1] / "schema" / "models.schema.json"
TABLE = (
    ObjectType(0x03, 0x41, 0, 0, 0),
    ObjectType(0x00, 0x01, 1, 0, 1),
    ObjectType(0x03, 0x01, 2, 0, 1),
)
CRAFT = (2, 7)


def textured(version: int = 2) -> bytes:
    """Return a model with a glowing texture, a switch of two, and one face."""
    glow = N(20, b"Lit", payload=Tex(2, 1, b"\0\1", palette=palette(color=glow_color)))
    dark = N(20, b"_Off", payload=Tex(1, 1, b"\1", shares=glow))
    record = Face((0, 1, 2), normal=(0.0, 0.0, 1.0))
    group = N(0, children=[
        N(3, count=3, payload=vecs([(0, 0, 0), (1, 0, 0), (0, 1, 0)])),
        N(24, children=[glow, dark]),
        N(1, count=1, payload=faces(version, [record], extra=[(0, 0, 1)] * 3)),
    ])  # fmt: skip
    return build_opt(
        [group, N(20, payload=Tex(1, 1, b"\0", shares=glow))], version=version
    )


@pytest.fixture
def tables(monkeypatch):
    """Replace the object tables with small synthetic ones."""
    for module in (objects_module, to_json_module):
        monkeypatch.setattr(module, "OBJECT_TYPES", TABLE)
        monkeypatch.setattr(module, "CRAFT_OBJECTS", CRAFT)


@pytest.fixture
def install(tmp_path):
    """Return a synthetic install: three model files and one spec list."""
    return write_files(
        tmp_path / "game",
        {
            "ivfiles/SHIP.OPT": textured(),
            "ivfiles/bad.opt": b"\xff\xff\xff\xff\0\0\0\0",
            "BalanceOfPower/IVFILES/new~1.opt": textured(2),
            "ivfiles/SPEC640.LST": b"ivfiles\\SHIP.OPT\r\nivfiles\\GONE.OPT\r\n",
        },
    )


@pytest.fixture(scope="module")
def validator():
    """Return a draft 2020-12 validator for the published schema file."""
    schema = json.loads(SCHEMA_FILE.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator.check_schema(schema)
    return jsonschema.Draft202012Validator(schema)


def exported(install) -> dict:
    """Return the JSON export of the install as it is."""
    return models_to_json(models_view(install), objects_view(install))


def test_the_export_describes_every_model(install, validator):
    data = exported(install)
    assert list(validator.iter_errors(data)) == []
    assert (data["format"], data["format_version"], data["lod"]) == (
        "jedimaster.models",
        1,
        1,
    )
    bad, new, ship = data["models"]
    assert ship == {
        "name": "ivfiles\\ship.opt",
        "status": "loaded",
        "file": "ivfiles/SHIP.OPT",
        "version": 2,
        "components": 1,
        "lods": 1,
        "switches": 2,
        "glb": ["ship.glb", "ship_s1.glb"],
        "textures": [
            {"node": 3, "name": "Lit", "width": 2, "height": 1, "glows": True},
            {"node": 4, "name": "_Off", "width": 1, "height": 1, "glows": False},
            {"node": 6, "name": "texture 6", "width": 1, "height": 1, "glows": False},
        ],
    }
    assert new["file"] == "BalanceOfPower/IVFILES/new~1.opt" and new["version"] == 2
    assert new["glb"] == ["new~1.glb", "new~1_s1.glb"]
    assert bad == {
        "name": "ivfiles\\bad.opt",
        "status": "not_loaded",
        "file": None,
        "version": None,
        "components": None,
        "lods": None,
        "switches": None,
        "glb": [],
        "textures": [],
    }


def test_the_export_names_each_types_model(tables, install):
    data = exported(install)
    assert data["objects"] == [
        {
            "type": 0,
            "model": "ivfiles\\SHIP.OPT",
            "model_name": "ivfiles\\ship.opt",
            "proving_grounds": True,
        },
        {"type": 1, "model": None, "model_name": None, "proving_grounds": False},
        {
            "type": 2,
            "model": "ivfiles\\GONE.OPT",
            "model_name": None,
            "proving_grounds": False,
        },
    ]
    assert data["crafts"] == [
        {"type": 0, "object": 2, "model": "ivfiles\\GONE.OPT", "model_name": None},
        {"type": 1, "object": 7, "model": None, "model_name": None},
    ]


def test_glb_stems():
    assert glb_stem("ivfiles\\Z-95.OPT") == "z-95"
    assert glb_stem("ivfiles\\a b+c.opt") == "a_b_c"
    assert glb_stem("ivfiles\\X.Y.OPT") == "x.y"


def test_schema_file_matches_model():
    text = json.dumps(models_schema(), indent=2) + "\n"
    assert SCHEMA_FILE.read_text(encoding="utf-8") == text, (
        "schema/models.schema.json is stale: run python tools/gen_schema.py"
    )


@pytest.mark.parametrize(
    ("path", "value"),
    [
        (("models", 0, "status"), "ok"),
        (("models", 2, "version"), 3),
        (("models", 2, "glb", 0), "Ship.glb"),
        (("models", 2, "textures", 0, "width"), 0),
        (("models", 2, "textures", 0, "glows"), 1),
        (("models", 2, "extra"), 1),
        (("objects", 0, "type"), 201),
        (("crafts", 0, "model"), 5),
        (("lod",), 2),
        (("models", 2, "name"), "ship.opt"),
    ],
)
def test_schema_rejects_bad_data(install, validator, path, value):
    data = copy.deepcopy(exported(install))
    target = data
    for key in path[:-1]:
        target = target[key]
    target[path[-1]] = value
    assert list(validator.iter_errors(data)), path


def test_schema_needs_every_type(install, validator):
    data = exported(install)
    for key, size in (("objects", 201), ("crafts", 96)):
        short = copy.deepcopy(data)
        short[key] = short[key][:1]
        assert list(validator.iter_errors(short)), key
        assert validator.schema["properties"][key]["minItems"] == size


def test_models_dump(install, capsys):
    path = install / "ivfiles" / "SHIP.OPT"
    assert main(["models", "dump", str(path)]) == 0
    out = capsys.readouterr().out.splitlines()
    assert out[0].startswith(f'model "{path}" file="{path}"')
    assert out[0].endswith(" version=2 roots=2 nodes=7 reserved=7")
    assert out[1:3] == ["root 0 node=0", "root 1 node=6"]
    assert main(["models", "dump", "ivfiles\\ship.opt", "--install", str(install)]) == 0
    assert capsys.readouterr().out.splitlines()[1:] == out[1:]
    assert main(["models", "dump", str(install / "ivfiles" / "bad.opt")]) == 1
    assert (
        main(["models", "dump", str(install / "none.opt"), "--install", str(install)])
        == 2
    )


def test_models_export(install, tmp_path, capsys, validator):
    out = tmp_path / "out"
    assert main(["models", "export", str(install), str(out)]) == 0
    assert "exported 4 .glb files and models.json" in capsys.readouterr().out
    names = sorted(p.name for p in out.iterdir())
    assert names == [
        "models.json",
        "new~1.glb",
        "new~1_s1.glb",
        "ship.glb",
        "ship_s1.glb",
    ]
    data = json.loads((out / "models.json").read_text(encoding="utf-8"))
    assert list(validator.iter_errors(data)) == [] and data["balance_of_power"] is True
    document, _ = read_glb((out / "ship_s1.glb").read_bytes())
    assert document["nodes"][0]["name"] == "ship"
    lit = [m["name"] for m in document["materials"]]
    assert lit == ["_Off"]
    base = tmp_path / "base"
    assert (
        main(["models", "export", str(install), str(base), "--no-balance-of-power"])
        == 0
    )
    assert sorted(p.name for p in base.iterdir()) == [
        "models.json",
        "ship.glb",
        "ship_s1.glb",
    ]
    assert json.loads((base / "models.json").read_text())["balance_of_power"] is False
    assert main(["models", "export", str(tmp_path / "nothing"), str(base)]) == 2
    (tmp_path / "file").write_text("x")
    assert main(["models", "export", str(install), str(tmp_path / "file" / "x")]) == 1
