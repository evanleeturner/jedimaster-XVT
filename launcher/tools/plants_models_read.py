"""The planted faults of the models' reading: the file, its nodes, its textures.

Purpose:
    Break one rule of ``jedimaster/models/`` per plant: the markers, the
    body's size and base, links and limits, node numbering and cycles,
    payload sizes and the context that sizes face data, and textures'
    texels and palettes.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``models-``.
    - Each ``old`` text occurs exactly once in its file.
    - No plant makes a read run forever: each fault ends in a refusal, a
      wrong value, or an error the suite reports.

Call:
    ``from plants_models_read import MODELS_READ_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

R = "jedimaster/models/reader.py"
B = "jedimaster/models/body.py"
T = "jedimaster/models/textures.py"
M = "jedimaster/models/model.py"
TR = "tests/test_models_reader.py::"
TT = "tests/test_models_textures.py::"
TD = "tests/test_models_draw.py::"
VERSIONS = TR + "test_each_version_marker"
SIDES = TT + "test_texture_sides_refused"
SIDES_LINE = "    if not (1 <= width <= MAX_SIDE and 1 <= height <= MAX_SIDE):"

_READER = [
    (
        "marker-v0-as-v1",
        R,
        "version, size, start = 0, marker, 4",
        "version, size, start = 1, marker, 4",
        (VERSIONS + "[0]", TR + "test_version_0_marker_is_the_size"),
    ),
    (
        "marker-version-from-marker",
        R,
        "version, size, start = -marker,",
        "version, size, start = 2,",
        (VERSIONS + "[1]",),
    ),
    (
        "marker-v2-as-v1",
        R,
        "version, size, start = -marker,",
        "version, size, start = 1,",
        (VERSIONS + "[2]", TD + "test_version_2_is_drawn_as_it_is"),
    ),
    (
        "marker-any-negative",
        R,
        "    elif marker in (-1, -2):",
        "    elif marker < 0:",
        (
            TR + "test_other_markers_refused[-3]",
            TR + "test_other_markers_refused[-100]",
        ),
    ),
    (
        "marker-zero-a-size",
        R,
        "    if marker > 0:",
        "    if marker >= 0:",
        (TR + "test_other_markers_refused[0]",),
    ),
    (
        "short-marker",
        R,
        "    if len(data) < 4:",
        "    if len(data) < 2:",
        (TR + "test_short_files_refused",),
    ),
    (
        "short-size",
        R,
        "        if len(data) < 8:",
        "        if len(data) < 5:",
        (TR + "test_short_files_refused",),
    ),
    ("body-min", R, "BODY_MIN = 14", "BODY_MIN = 13", (TR + "test_body_size_limits",)),
    (
        "body-max",
        R,
        "    if not BODY_MIN <= size <= BODY_MAX:",
        "    if not BODY_MIN <= size:",
        (TR + "test_body_size_limits",),
    ),
    (
        "body-size-remains",
        R,
        "    if size != len(data) - start:",
        "    if size > len(data) - start:",
        (
            TR + "test_body_size_must_be_what_remains",
            TR + "test_version_0_marker_is_the_size",
        ),
    ),
    (
        "base-limit",
        R,
        "    if base + len(raw) > ADDRESS_MAX:",
        "    if base + len(raw) > ADDRESS_MAX + 1:",
        (TR + "test_base_plus_size_may_not_pass_the_address_space",),
    ),
    (
        "base-ignored",
        R,
        '    base = struct.unpack_from("<I", raw)[0]',
        "    base = 0x10000",
        (TR + "test_links_are_addresses_from_the_base",),
    ),
    (
        "reserved-offset",
        R,
        'reserved = struct.unpack_from("<H", raw, 4)[0]',
        'reserved = struct.unpack_from("<H", raw, 5)[0]',
        (VERSIONS,),
    ),
    (
        "root-count-max",
        R,
        "    if not 0 <= root_count <= MAX_ROOTS:",
        "    if not 0 <= root_count < MAX_ROOTS:",
        (TR + "test_root_count_limits",),
    ),
    (
        "root-count-negative",
        R,
        "    if not 0 <= root_count <= MAX_ROOTS:",
        "    if not -1 <= root_count <= MAX_ROOTS:",
        (TR + "test_root_count_limits",),
    ),
    (
        "root-table-always",
        R,
        "    if root_count:\n",
        "    if True:\n",
        (TR + "test_root_table_must_lie_in_the_body",),
    ),
    (
        "root-empty-slot",
        R,
        "        if link == 0:\n            roots.append(None)\n            continue\n",
        "",
        (TR + "test_empty_root_and_child_slots",),
    ),
    (
        "roots-not-siblings",
        R,
        "        number, context = walker.visit(link, 1, context)",
        "        number, _ = walker.visit(link, 1, context)",
        (TR + "test_the_context_rule_that_sizes_extra_normals",),
    ),
    (
        "child-empty-slot",
        R,
        "            if child == 0:\n                children.append(None)\n",
        "            if child == 0:\n",
        (TR + "test_empty_root_and_child_slots",),
    ),
    (
        "reached-again-new-node",
        R,
        "        if offset in self.numbers:",
        "        if False:",
        (
            TR + "test_first_visit_numbering_with_a_node_reached_twice",
            TR + "test_a_node_reached_again_keeps_its_first_size",
        ),
    ),
    (
        "cycle-allowed",
        R,
        "        if offset in self.inside:",
        "        if False:",
        (TR + "test_a_cycle_is_refused",),
    ),
    (
        "depth-limit",
        R,
        "        if depth > MAX_DEPTH:",
        "        if depth > MAX_DEPTH + 1:",
        (TR + "test_nesting_limit",),
    ),
    (
        "depth-value",
        R,
        "MAX_DEPTH = 256",
        "MAX_DEPTH = 257",
        (TR + "test_the_limits_are_the_games", TR + "test_nesting_limit"),
    ),
    (
        "node-limit",
        R,
        "        if number >= MAX_NODES:",
        "        if number > MAX_NODES:",
        (TR + "test_node_count_limit",),
    ),
    (
        "node-value",
        R,
        "MAX_NODES = 65536",
        "MAX_NODES = 65535",
        (TR + "test_the_limits_are_the_games",),
    ),
    (
        "child-count-max",
        R,
        "        if not 0 <= count <= MAX_CHILDREN:",
        "        if not 0 <= count < MAX_CHILDREN:",
        (TR + "test_child_count_limits",),
    ),
    (
        "child-count-negative",
        R,
        "        if not 0 <= count <= MAX_CHILDREN:",
        "        if not -1 <= count <= MAX_CHILDREN:",
        (TR + "test_child_count_limits",),
    ),
    (
        "child-table-for-none",
        R,
        "        if count == 0:\n            return ()\n",
        "",
        (TR + "test_child_table_must_lie_in_the_body",),
    ),
    (
        "payload-count-max",
        R,
        "        if not 0 <= count <= MAX_PAYLOAD_COUNT:",
        "        if not 0 <= count < MAX_PAYLOAD_COUNT:",
        (TR + "test_payload_count_limits",),
    ),
    (
        "payload-count-negative",
        R,
        "        if not 0 <= count <= MAX_PAYLOAD_COUNT:",
        "        if not -1 <= count <= MAX_PAYLOAD_COUNT:",
        (TR + "test_payload_count_limits",),
    ),
    (
        "payload-count-value",
        R,
        "MAX_PAYLOAD_COUNT = 1_000_000",
        "MAX_PAYLOAD_COUNT = 999_999",
        (TR + "test_the_limits_are_the_games", TR + "test_payload_count_limits"),
    ),
    (
        "payload-of-a-group",
        R,
        "        if kind not in PAYLOAD_TYPES:",
        "        if kind not in PAYLOAD_TYPES and kind != 0:",
        (TR + "test_payload_link_rules",),
    ),
    (
        "reference-count-kept",
        R,
        "return replace(node, count=0, payload=name_ref",
        "return replace(node, count=count, payload=name_ref",
        (TR + "test_name_reference_keeps_count_0_and_reads_its_name",),
    ),
    (
        "reference-cut-short",
        R,
        "self.body.data[start : end if end >= 0 else None]",
        "self.body.data[start:end]",
        (TR + "test_name_reference_keeps_count_0_and_reads_its_name",),
    ),
    (
        "normals-met-ignored",
        R,
        "extra = 0 if context.normals_met else context.vertex_count",
        "extra = context.vertex_count",
        (TR + "test_the_context_rule_that_sizes_extra_normals",),
    ),
    (
        "vertex-count-ignored",
        R,
        "        return _Context(count, context.normals_met)",
        "        return _Context(context.vertex_count, context.normals_met)",
        (TR + "test_extra_normals_follow_the_last_vertex_count",),
    ),
    (
        "normals-never-met",
        R,
        "        return _Context(context.vertex_count, True)",
        "        return _Context(context.vertex_count, False)",
        (TR + "test_the_context_rule_that_sizes_extra_normals",),
    ),
    (
        "children-leak-context",
        R,
        "        return number, context\n\n    def _child_links",
        "        return number, inner\n\n    def _child_links",
        (TR + "test_the_context_rule_that_sizes_extra_normals",),
    ),
    (
        "siblings-blind",
        R,
        "            child_number, inner = self.visit(child, depth + 1, inner)",
        "            child_number, _ = self.visit(child, depth + 1, inner)",
        (TR + "test_extra_normals_follow_the_last_vertex_count",),
    ),
]

_BODY = [
    (
        "link-zero-allowed",
        B,
        "        if link == 0 or not 0 <= offset < self.size:",
        "        if not 0 <= offset < self.size:",
        (TR + "test_payload_link_rules",),
    ),
    (
        "link-at-the-end",
        B,
        "        if link == 0 or not 0 <= offset < self.size:",
        "        if link == 0 or not 0 <= offset <= self.size:",
        (TR + "test_root_link_outside_the_body_refused[42]",),
    ),
    (
        "link-below-base",
        B,
        "        if link == 0 or not 0 <= offset < self.size:",
        "        if link == 0 or not -4 <= offset < self.size:",
        (
            TR + "test_root_link_outside_the_body_refused[-4]",
            TR + "test_root_link_outside_the_body_refused[-1]",
        ),
    ),
    (
        "need-start-only",
        B,
        "        if offset < 0 or size < 0 or offset + size > self.size:",
        "        if offset < 0 or size < 0 or offset > self.size:",
        (TR + "test_node_must_fit_in_the_body",),
    ),
    (
        "cut-not-padded",
        B,
        "        return data + bytes(size - len(data))",
        "        return data",
        (
            TR + "test_a_payload_cut_by_the_body_end_reads_as_zeros",
            TR + "test_extra_normals_read_as_zero_past_the_body",
        ),
    ),
    (
        "name-without-end",
        B,
        "        if end < 0:",
        "        if end < -1:",
        (TR + "test_names_must_end_inside_the_body",),
    ),
]

_MODEL = [
    (
        "record-sizes",
        M,
        "RECORD_SIZES = {0: 48, 1: 64, 2: 64}",
        "RECORD_SIZES = {0: 64, 1: 48, 2: 48}",
        (TR + "test_face_payload_sized_by_version",),
    ),
    (
        "transform-size",
        M,
        "    TRANSFORM: 48,",
        "    TRANSFORM: 44,",
        (TR + "test_fixed_and_counted_payload_sizes",),
    ),
    (
        "materials-size",
        M,
        "MATERIALS: 56,",
        "MATERIALS: 48,",
        (TR + "test_fixed_and_counted_payload_sizes",),
    ),
    (
        "top-is-all-texels",
        M,
        "        return self.texels[: self.width * self.height]",
        "        return self.texels",
        (TT + "test_texels_with_levels_take_the_data_size",),
    ),
    (
        "colors-skip-shading",
        M,
        "        start = 256 * self.sub_palettes + 512 * sub_palette\n"
        '        return list(struct.unpack_from("<256H"',
        "        start = 512 * sub_palette\n"
        '        return list(struct.unpack_from("<256H"',
        (TT + "test_inline_palettes",),
    ),
    (
        "v0-normals-from-coords",
        M,
        "quads[3 if groups > 3 else 0]",
        "quads[3 if groups > 3 else 2]",
        (TD + "test_version_0_normal_indices_are_the_vertex_indices",),
    ),
    (
        "coords-one-pair",
        M,
        'struct.unpack_from("<2f", node.payload, 8 * i)',
        'struct.unpack_from("<2f", node.payload, 0)',
        (TD + "test_a_face_takes_its_corners_from_the_state_lists",),
    ),
]

_TEXTURES = [
    (
        "texels-never-levels",
        T,
        "    count = data_size if texture_size == pixels else pixels",
        "    count = pixels",
        (
            TT + "test_texels_with_levels_take_the_data_size",
            TT + "test_fewer_texel_bytes_than_the_picture_refused",
        ),
    ),
    (
        "texels-always-levels",
        T,
        "    count = data_size if texture_size == pixels else pixels",
        "    count = data_size",
        (TT + "test_texels_without_levels_take_width_times_height",),
    ),
    (
        "texels-too-few",
        T,
        "    if count < pixels:",
        "    if count < 0:",
        (TT + "test_fewer_texel_bytes_than_the_picture_refused",),
    ),
    (
        "height-unchecked",
        T,
        SIDES_LINE,
        "    if not (1 <= width <= MAX_SIDE):",
        (SIDES + "[1-0]", SIDES + "[1-16385]"),
    ),
    (
        "width-zero",
        T,
        SIDES_LINE,
        "    if not (width <= MAX_SIDE and 1 <= height <= MAX_SIDE):",
        (SIDES + "[0-1]",),
    ),
    (
        "width-unbounded",
        T,
        SIDES_LINE,
        "    if not (1 <= width and 1 <= height <= MAX_SIDE):",
        (SIDES + "[16385-1]",),
    ),
    (
        "side-limit",
        T,
        "MAX_SIDE = 16384",
        "MAX_SIDE = 16383",
        (TT + "test_largest_sides_accepted",),
    ),
    (
        "inline-unbounded",
        T,
        "    if not 0 <= inline <= MAX_INLINE:",
        "    if not 0 <= inline:",
        (TT + "test_inline_palette_count_refused[17]",),
    ),
    (
        "inline-negative",
        T,
        "    if not 0 <= inline <= MAX_INLINE:",
        "    if not -1 <= inline <= MAX_INLINE:",
        (TT + "test_inline_palette_count_refused[-1]",),
    ),
    (
        "own-palette-missed",
        T,
        "    elif link - body.base == end:",
        "    elif link - body.base == end + 1:",
        (TT + "test_own_palette",),
    ),
    (
        "inline-palette-size",
        T,
        "inline * SUB_PALETTE_BYTES",
        "inline * 512",
        (TT + "test_inline_palettes",),
    ),
    (
        "carrier-by-link",
        T,
        "carriers.setdefault(texture.palette_link - body.base, node)",
        "carriers.setdefault(texture.palette_link, node)",
        (TT + "test_shared_palettes_earlier_and_later",),
    ),
    (
        "shared-names-itself",
        T,
        "palette_node=carrier.number",
        "palette_node=node.number",
        (TT + "test_shared_palettes_earlier_and_later",),
    ),
    (
        "any-carrier-will-do",
        T,
        "        carrier = carriers.get(texture.palette_link - body.base)",
        "        carrier = carriers.get(texture.palette_link - body.base) or next(\n"
        "            iter(carriers.values()), None\n        )",
        (TT + "test_an_unowned_palette_link_is_refused",),
    ),
    (
        "block-padded",
        T,
        "palette = body.need(end, BLOCK_SIZE, ",
        "palette = body.padded(end, BLOCK_SIZE) or (",
        (TT + "test_palettes_and_texels_must_lie_in_the_body",),
    ),
    (
        "header-padded",
        T,
        "    header = body.need(offset, HEADER_SIZE, ",
        "    header = body.padded(offset, HEADER_SIZE) or (",
        (TT + "test_palettes_and_texels_must_lie_in_the_body",),
    ),
]


def _plants(rows: list[tuple]) -> list[Plant]:
    return [
        Plant(f"models-{i}", path, old, new, expect)
        for i, path, old, new, expect in rows
    ]


MODELS_READ_PLANTS: list[Plant] = _plants(_READER + _BODY + _MODEL + _TEXTURES)
