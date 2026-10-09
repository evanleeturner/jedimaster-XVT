"""The planted faults of the models' outputs: sheets, glTF, JSON, objects, commands.

Purpose:
    Break one rule of ``jedimaster/models/`` per plant: the install's
    model list and views, the three sheets' lines, the spec lists and the
    object flags, the glTF file (frame, winding, normals, materials,
    pictures, nodes, the GLB container), the JSON export and each schema
    rule the rejection test names, and the ``models`` command line.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``models-``.
    - Each ``old`` text occurs exactly once in its file.
    - A plant that changes the schema builder regenerates the schema files,
      so it fails the rejection test it names, not the drift test.

Call:
    ``from plants_models_output import MODELS_OUTPUT_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import CLI
from plants_common import Plant

logger = logging.getLogger(__name__)

RD = "jedimaster/models/render.py"
GL = "jedimaster/models/gltf.py"
JS = "jedimaster/models/to_json.py"
OB = "jedimaster/models/objects.py"
GM = "jedimaster/models/game.py"
CL = "jedimaster/models/cli.py"
TB = "jedimaster/models/tables.py"
TS = "tests/test_models_sheets.py::"
TG = "tests/test_models_gltf.py::"
TE = "tests/test_models_export.py::"
TD = "tests/test_models_draw.py::"
TR = "tests/test_models_reader.py::"
TT = "tests/test_models_textures.py::"
EVERY_LINE = TS + "test_every_line_of_the_file_sheet"
VIEWS = TS + "test_each_view_resolves_every_name"
DRAW_SHEET = TS + "test_the_draw_sheet_prints_passes_components_and_sorted_faces"
OBJECTS_SHEET = TS + "test_the_objects_sheet"
TYPE_MODELS = TS + "test_object_and_craft_models"
MATERIALS = TG + "test_one_material_per_texture_and_the_glow_rule"
DESCRIBES = TE + "test_the_export_describes_every_model"
NAMES = TE + "test_the_export_names_each_types_model"
REJECTS = TE + "test_schema_rejects_bad_data"
EXPORT = TE + "test_models_export"
DUMP = TE + "test_models_dump"

_RENDER = [
    (
        "number-nan-sign",
        RD,
        'return "-nan" if math.copysign(1.0, value) < 0 else "nan"',
        'return "nan"',
        (TS + "test_numbers_and_quotes_as_the_sheets_print_them",),
    ),
    (
        "number-8-digits",
        RD,
        'return f"{value:.9g}"',
        'return f"{value:.8g}"',
        (TS + "test_numbers_and_quotes_as_the_sheets_print_them",),
    ),
    (
        "quote-backslash-raw",
        RD,
        "if byte in (0x22, 0x5C):",
        "if byte == 0x22:",
        (TS + "test_numbers_and_quotes_as_the_sheets_print_them", VIEWS),
    ),
    (
        "quote-delete-raw",
        RD,
        "elif 32 <= byte <= 126:",
        "elif 32 <= byte <= 127:",
        (TS + "test_numbers_and_quotes_as_the_sheets_print_them",),
    ),
    (
        "children-slot-blank",
        RD,
        '",".join("-" if c is None else str(c)',
        '",".join("" if c is None else str(c)',
        (EVERY_LINE,),
    ),
    (
        "root-slot-none",
        RD,
        "node={'-' if root is None else root}",
        "node={root}",
        (TR + "test_empty_root_and_child_slots",),
    ),
    (
        "palette-never-inline",
        RD,
        'carrier = "inline" if texture.inline_palettes else "none"',
        'carrier = "none"',
        (TT + "test_inline_palettes", EVERY_LINE),
    ),
    (
        "block-for-two-only",
        RD,
        "if texture.sub_palettes != FULL_BLOCK:",
        "if texture.sub_palettes < 2:",
        (TT + "test_inline_palettes",),
    ),
    (
        "glow-hash-big-endian",
        RD,
        'glow_bytes = struct.pack("<256H", *glow.colors)',
        'glow_bytes = struct.pack(">256H", *glow.colors)',
        (TT + "test_glow_fields_of_the_file_sheet",),
    ),
    (
        "faces-extra-count",
        RD,
        'f"extra_normals={node.extra_normals} extra=',
        'f"extra_normals={0} extra=',
        (TS + "test_the_faces_line_counts_extra_normals",),
    ),
    (
        "descriptor-target-id",
        RD,
        'target_id = struct.unpack_from("<i", node.payload, 56)[0]',
        'target_id = struct.unpack_from("<i", node.payload, 60)[0]',
        (EVERY_LINE,),
    ),
    (
        "hardpoint-at",
        RD,
        'at = struct.unpack_from("<3f", node.payload, 4)',
        'at = struct.unpack_from("<3f", node.payload, 0)',
        (EVERY_LINE,),
    ),
    (
        "base-color-no-values",
        RD,
        "VALUE_TYPES = (TRANSFORM, ROTATION, ROTATION_SCALE, 4, 6, 19)",
        "VALUE_TYPES = (TRANSFORM, ROTATION, ROTATION_SCALE, 4, 6)",
        (EVERY_LINE,),
    ),
    (
        "reserved-is-version",
        RD,
        "reserved={model.reserved}",
        "reserved={model.version}",
        (VIEWS, EVERY_LINE),
    ),
    (
        "missing-word",
        RD,
        'return f"model {name} missing"',
        'return f"model {name} absent"',
        (VIEWS, DRAW_SHEET),
    ),
    (
        "not-loaded-silent",
        RD,
        'return head if entry.status == LOADED else f"{head} not_loaded"',
        "return head",
        (VIEWS,),
    ),
    (
        "faces-unsorted",
        RD,
        'lines += sorted(faces, key=lambda text: text.encode("latin-1"))',
        "lines += faces",
        (DRAW_SHEET,),
    ),
    (
        "components-are-roots",
        RD,
        "count = sum(1 for n in components(model) if n >= 0)",
        "count = len(model.roots)",
        (TD + "test_faces_under_a_texture_root_are_component_minus_1",),
    ),
    (
        "pass-faces-short",
        RD,
        'f"faces={total} walk=unchecked"',
        'f"faces={total - 1} walk=unchecked"',
        (DRAW_SHEET,),
    ),
    (
        "bad-as-nan",
        RD,
        '";".join("bad" if v is None else',
        '";".join("nan" if v is None else',
        (TD + "test_a_value_whose_index_is_outside_its_list_is_bad",),
    ),
    (
        "unnamed-texture-white",
        RD,
        'tex = "-" if name is None else',
        'tex = "white" if name is None else',
        (TD + "test_face_lines_name_their_texture",),
    ),
    (
        "record-flags-one-digit",
        RD,
        "record_flags=0x{o.record_flags:02x}",
        "record_flags=0x{o.record_flags:x}",
        (OBJECTS_SHEET,),
    ),
    (
        "craft-line",
        RD,
        'f"craft {c} object={t}"',
        'f"craft {c} type={t}"',
        (OBJECTS_SHEET,),
    ),
    (
        "missing-list-word",
        RD,
        'lines.append(f"{head} missing")',
        'lines.append(f"{head} file=-")',
        (OBJECTS_SHEET,),
    ),
    (
        "entries-from-1",
        RD,
        'f"entry {group} {size} {i} {quote(line)}"',
        'f"entry {group} {size} {i + 1} {quote(line)}"',
        (OBJECTS_SHEET,),
    ),
]

_GLTF = [
    (
        "turn-keeps-x",
        GL,
        "    return (-x, z, -y)",
        "    return (x, z, -y)",
        (
            TG + "test_the_vector_helpers",
            TG + "test_positions_are_turned_with_their_bounds",
        ),
    ),
    (
        "wind-backwards",
        GL,
        "return [0, 2, 1] if facing < 0 else [0, 1, 2]",
        "return [0, 2, 1] if facing > 0 else [0, 1, 2]",
        (
            TG + "test_the_vector_helpers",
            TG + "test_triangles_wind_toward_the_turned_face_normal",
        ),
    ),
    (
        "quad-split-wrong",
        GL,
        "([[0, 2, 3]] if len(face.corners) == 4 else [])",
        "([[1, 2, 3]] if len(face.corners) == 4 else [])",
        (TG + "test_triangles_wind_toward_the_turned_face_normal",),
    ),
    (
        "zero-normal-divided",
        GL,
        "    if length == 0.0 or not math.isfinite(length):",
        "    if length < 0.0:",
        (TG + "test_the_vector_helpers",),
    ),
    (
        "normals-not-scaled",
        GL,
        "    return (vector[0] / length, vector[1] / length, vector[2] / length)",
        "    return vector",
        (TG + "test_normals_are_unit_length_and_zero_ones_replaced",),
    ),
    (
        "zero-normal-up",
        GL,
        "self.normals.append(unit(normal, face_normal))",
        "self.normals.append(unit(normal, UP))",
        (TG + "test_normals_are_unit_length_and_zero_ones_replaced",),
    ),
    (
        "bad-uv-kept-bad",
        GL,
        "corner.uv if corner.uv is not None else (0.0, 0.0)",
        "corner.uv if corner.uv is not None else (0.5, 0.5)",
        (TG + "test_bad_values_in_the_export",),
    ),
    (
        "bad-face-gets-material",
        GL,
        "        if any(c.position is None for c in face.corners):\n            logger",
        "        if False:\n            logger",
        (TG + "test_bad_values_in_the_export",),
    ),
    (
        "bounds-swapped",
        GL,
        'accessor["min"] = [min(row[k]',
        'accessor["min"] = [max(row[k]',
        (TG + "test_positions_are_turned_with_their_bounds",),
    ),
    (
        "glow-for-taken-texture",
        GL,
        "lit = number is not None and face.named and glows(self.model, number)",
        "lit = number is not None and glows(self.model, number)",
        (MATERIALS,),
    ),
    (
        "underscore-glows",
        GL,
        'node.name.startswith(b"_")',
        'node.name.startswith(b"-")',
        (MATERIALS,),
    ),
    ("unnamed-checked-for-underscore", GL, "node.name is None or ", "", (MATERIALS,)),
    (
        "white-transparent",
        GL,
        'pbr["baseColorFactor"] = [1, 1, 1, 1]',
        'pbr["baseColorFactor"] = [1, 1, 1, 0]',
        (MATERIALS,),
    ),
    (
        "emissive-factor",
        GL,
        '"emissiveFactor": [1, 1, 1],',
        '"emissiveFactor": [1, 1, 0],',
        (MATERIALS,),
    ),
    (
        "base-from-sub-palette-0",
        GL,
        "texture.colors(base_sub_palette(texture))",
        "texture.colors(0)",
        (TG + "test_pictures_inflate_to_the_texture_colors",),
    ),
    (
        "dark-texels-clear",
        GL,
        'if lit else b"\\0\\0\\0\\xff"',
        'if lit else b"\\0\\0\\0\\0"',
        (TG + "test_pictures_inflate_to_the_texture_colors",),
    ),
    (
        "pictures-repeated",
        GL,
        "        if key not in self.pictures:",
        "        if True:",
        (TG + "test_pictures_inflate_to_the_texture_colors",),
    ),
    (
        "hardpoint-name",
        GL,
        '{"name": f"hardpoint {kind}"',
        '{"name": f"hardpoint{kind}"',
        (TG + "test_nodes_hardpoints_and_extras",),
    ),
    (
        "extras-target-id",
        GL,
        'target_id = struct.unpack_from("<i", payload, 56)[0]',
        'target_id = struct.unpack_from("<i", payload, 60)[0]',
        (TG + "test_nodes_hardpoints_and_extras",),
    ),
    (
        "empty-mesh-kept",
        GL,
        '"primitives": out} if out else None',
        '"primitives": out}',
        (TG + "test_nodes_hardpoints_and_extras",),
    ),
    (
        "component-name",
        GL,
        '{"name": f"component {component.number}"}',
        '{"name": f"part {component.number}"}',
        (TG + "test_nodes_hardpoints_and_extras",),
    ),
    (
        "unnamed-texture-name",
        GL,
        'return f"texture {number}" if name is None',
        'return f"texture{number}" if name is None',
        (MATERIALS,),
    ),
    (
        "glb-length-short",
        GL,
        "GLB_VERSION, 12 + len(chunks))",
        "GLB_VERSION, len(chunks))",
        (TG + "test_nodes_hardpoints_and_extras",),
    ),
    (
        "json-padded-with-zeros",
        GL,
        'text += b" " * (-len(text) % 4)',
        'text += b"\\0" * (-len(text) % 4)',
        (TG + "test_nodes_hardpoints_and_extras",),
    ),
    (
        "views-not-aligned",
        GL,
        "        while len(self.data) % 4:",
        "        while False:",
        (TG + "test_the_export_draws_detail_level_1",),
    ),
    (
        "switch-file-names",
        GL,
        '[f"{stem}_s{s}.glb" for s',
        '[f"{stem}-s{s}.glb" for s',
        (TG + "test_switch_files_and_a_model_with_no_faces",),
    ),
    (
        "export-level-2",
        GL,
        "EXPORT_LOD = 1",
        "EXPORT_LOD = 2",
        (TG + "test_the_export_draws_detail_level_1",),
    ),
]

_JSON = [
    (
        "stem-keeps-case",
        JS,
        'name.replace("\\\\", "/")).stem.lower()',
        'name.replace("\\\\", "/")).stem',
        (TE + "test_glb_stems",),
    ),
    (
        "stem-keeps-spaces",
        JS,
        'UNSAFE = re.compile(r"[^a-z0-9_.~-]")',
        'UNSAFE = re.compile(r"[^a-z0-9_.~ +-]")',
        (TE + "test_glb_stems",),
    ),
    (
        "file-is-sheet-label",
        JS,
        '"file": _relative(entry.path, install) if model',
        '"file": entry.file if model',
        (DESCRIBES,),
    ),
    (
        "components-are-roots-json",
        JS,
        '"components": sum(1 for n in components(model) if n >= 0)',
        '"components": len(model.roots)',
        (DESCRIBES,),
    ),
    (
        "glows-ignores-name",
        JS,
        '"glows": glows(model, node.number)',
        '"glows": glow_of(node.texture).glows',
        (DESCRIBES,),
    ),
    (
        "model-name-unchecked",
        JS,
        "return lowered if lowered in names else None",
        "return lowered",
        (NAMES,),
    ),
    (
        "proving-grounds-flag",
        JS,
        "bool(record.asset_flags & PROVING_GROUNDS_FLAG)",
        "bool(record.asset_flags & 0x20)",
        (NAMES,),
    ),
    (
        "craft-object-is-type",
        JS,
        '"object": craft_object(craft_type)',
        '"object": craft_type',
        (NAMES,),
    ),
]


def _schema(pid: str, old: str, new: str, case: str) -> Plant:
    return Plant(f"models-schema-{pid}", JS, old, new, (f"{REJECTS}[{case}]",), True)


_SCHEMA = [
    _schema(
        "status",
        '"status": {"enum": [LOADED, MISSING, NOT_LOADED]}',
        '"status": {"type": "string"}',
        "path0-ok",
    ),
    _schema(
        "version",
        'nullable({"enum": [0, 1, 2]})',
        'nullable({"type": "integer"})',
        "path1-3",
    ),
    _schema(
        "glb-pattern",
        'GLB_PATTERN = r"^[a-z0-9_.~-]+(_s[1-9][0-9]*)?\\.glb$"',
        'GLB_PATTERN = r"^.+\\.glb$"',
        "path2-Ship.glb",
    ),
    _schema(
        "width",
        'ints("width", "height", minimum=1,',
        'ints("width", "height", minimum=0,',
        "path3-0",
    ),
    _schema(
        "glows",
        '"glows": {"type": "boolean"}',
        '"glows": {"type": ["boolean", "integer"]}',
        "path4-1",
    ),
    _schema(
        "model-open",
        'return {"Texture": texture, "Model": model,',
        'return {"Texture": texture, "Model": {**model, "additionalProperties": True},',
        "path5-1",
    ),
    _schema(
        "object-type",
        'ints("type", maximum=len(OBJECT_TYPES) - 1)',
        'ints("type", maximum=len(OBJECT_TYPES))',
        "path6-201",
    ),
    _schema(
        "craft-model",
        '"object": nullable({"type": "integer", "minimum": 0}),\n'
        '            "model": path,',
        '"object": nullable({"type": "integer", "minimum": 0}),\n'
        '            "model": {},',
        "path7-5",
    ),
    _schema("lod", '"lod": {"const": 1}', '"lod": {"type": "integer"}', "path8-2"),
    _schema(
        "name",
        '"name": {"type": "string", "pattern": r"^ivfiles\\\\"}',
        '"name": {"type": "string"}',
        "path9-ship.opt",
    ),
    Plant(
        "models-schema-min-items",
        JS,
        '"minItems": len(OBJECT_TYPES),',
        '"minItems": 0,',
        (TE + "test_schema_needs_every_type",),
        True,
    ),
    Plant(
        "models-schema-drift",
        JS,
        '"title": "XvT/BoP 3D models (jedimaster export)"',
        '"title": "XvT/BoP models"',
        (TE + "test_schema_file_matches_model",),
    ),
]

_OBJECTS = [
    (
        "list-0-byte-kept",
        OB,
        'LINE_ENDS = (b"\\n", b"\\r", b"\\0")',
        'LINE_ENDS = (b"\\n", b"\\r")',
        (TS + "test_list_lines_are_cut_and_counted_as_the_game_reads_them",),
    ),
    (
        "list-blank-counted",
        OB,
        "        if line:",
        "        if True:",
        (TS + "test_list_lines_are_cut_and_counted_as_the_game_reads_them",),
    ),
    (
        "list-lines-by-crlf",
        OB,
        'for raw in data.split(b"\\n"):',
        'for raw in data.split(b"\\r\\n"):',
        (TS + "test_list_lines_are_cut_and_counted_as_the_game_reads_them",),
    ),
    (
        "list-name-lowercase",
        OB,
        '{size}.LST"',
        '{size}.lst"',
        (TS + "test_list_lines_are_cut_and_counted_as_the_game_reads_them",),
    ),
    (
        "record-flag-ignored",
        OB,
        "        record.record_flags & MODEL_RECORD_FLAG\n",
        "        True\n",
        (TS + "test_the_flags_that_name_a_model",),
    ),
    (
        "asset-flag-textures",
        OB,
        "and record.asset_flags & MODEL_ASSET_FLAG",
        "and record.asset_flags & 0x03",
        (TS + "test_the_flags_that_name_a_model",),
    ),
    (
        "object-negative",
        OB,
        "    if not 0 <= object_type < len(OBJECT_TYPES):",
        "    if object_type >= len(OBJECT_TYPES):",
        (TYPE_MODELS,),
    ),
    (
        "line-beyond",
        OB,
        "not 0 <= record.resource_index < len(spec.lines)",
        "record.resource_index > len(spec.lines)",
        (TYPE_MODELS,),
    ),
    (
        "size-ignored",
        OB,
        "spec = view.lists.get((record.texture_group, size))",
        "spec = view.lists.get((record.texture_group, DEFAULT_SIZE))",
        (TYPE_MODELS,),
    ),
    (
        "craft-negative",
        OB,
        "    if not 0 <= craft_type < len(CRAFT_OBJECTS):",
        "    if craft_type >= len(CRAFT_OBJECTS):",
        (TYPE_MODELS,),
    ),
    (
        "list-label-base",
        OB,
        'return f"{BALANCE_OF_POWER}/{label}"',
        "return label",
        (OBJECTS_SHEET,),
    ),
    (
        "list-label-lowercase",
        OB,
        'label = game_path.replace("\\\\", "/")',
        'label = game_path.replace("\\\\", "/").lower()',
        (OBJECTS_SHEET,),
    ),
    (
        "lists-always-bop",
        OB,
        "read_spec_list(install, group, size, balance_of_power)",
        "read_spec_list(install, group, size, True)",
        (TYPE_MODELS,),
    ),
    (
        "table-short",
        TB,
        "    ObjectType(0x00, 0x00,   0, 0,   0),  # 200\n",
        "",
        (TS + "test_the_tables_cover_every_type",),
    ),
]

_VIEW = [
    (
        "names-keep-case",
        GM,
        "raw = os.fsencode(entry).lower()",
        "raw = os.fsencode(entry)",
        (TS + "test_the_list_joins_both_folders_lowercased_and_sorted",),
    ),
    (
        "names-any-file",
        GM,
        "if raw.endswith(SUFFIX) and (folder / entry).is_file():",
        "if (folder / entry).is_file():",
        (TS + "test_the_list_joins_both_folders_lowercased_and_sorted",),
    ),
    (
        "names-base-only",
        GM,
        "names = set(_opt_names(base)) | set(_opt_names(bop_folder))",
        "names = set(_opt_names(base))",
        (TS + "test_the_list_joins_both_folders_lowercased_and_sorted",),
    ),
    (
        "names-byte-order",
        GM,
        "key=name_order)",
        "key=str)",
        (TS + "test_the_list_joins_both_folders_lowercased_and_sorted",),
    ),
    (
        "view-always-bop",
        GM,
        "path = resolve(install, name, balance_of_power)",
        "path = resolve(install, name, True)",
        (VIEWS,),
    ),
    (
        "refusal-escapes",
        GM,
        "except (ModelFormatError, OSError) as exc:",
        "except OSError as exc:",
        (VIEWS,),
    ),
    ("label-is-name", GM, "sheet_file(install, name, path))", "name)", (VIEWS,)),
]

_COMMAND = [
    (
        "dump-missing-1",
        CL,
        'logger.error("model not found: %s", args.opt)\n        return 2',
        'logger.error("model not found: %s", args.opt)\n        return 1',
        (DUMP,),
    ),
    (
        "dump-refused-0",
        CL,
        'logger.error("model %s not loaded: %s", args.opt, entry.error)\n        return 1',
        'logger.error("model %s not loaded: %s", args.opt, entry.error)\n        return 0',
        (DUMP,),
    ),
    (
        "export-one-switch",
        CL,
        "names = glb_names(stem, most_children(entry.model, NODE_SWITCH))",
        "names = glb_names(stem, 1)",
        (EXPORT,),
    ),
    (
        "export-always-bop",
        CL,
        "balance_of_power = not args.no_balance_of_power",
        "balance_of_power = True",
        (EXPORT,),
    ),
    (
        "export-no-install-1",
        CL,
        'logger.error("not an install: %s", args.install)\n        return 2',
        'logger.error("not an install: %s", args.install)\n        return 1',
        (EXPORT,),
    ),
    (
        "export-oserror-escapes",
        CL,
        "except (OSError, ValueError) as exc:",
        "except ValueError as exc:",
        (EXPORT,),
    ),
    (
        "export-switch-0-only",
        CL,
        "glb_bytes(entry.model, stem, switch)",
        "glb_bytes(entry.model, stem, 0)",
        (EXPORT,),
    ),
    ("command-not-added", CLI, "    models_cli.add_parser(sub)\n", "", (DUMP, EXPORT)),
]


def _plants(rows: list[tuple]) -> list[Plant]:
    return [
        Plant(f"models-{i}", path, old, new, expect)
        for i, path, old, new, expect in rows
    ]


MODELS_OUTPUT_PLANTS: list[Plant] = [
    *_plants(_RENDER + _GLTF + _JSON),
    *_SCHEMA,
    *_plants(_OBJECTS + _VIEW + _COMMAND),
]
