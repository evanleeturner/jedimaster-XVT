"""The planted faults of the models' drawing: colors, glow, the walk, the rewrite.

Purpose:
    Break one rule of ``jedimaster/models/`` per plant: the 565 colors and
    the glow rule, the drawing walk's state and picks, name references,
    and the load-time rewrite of version 0 and 1 models.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``models-``.
    - Each ``old`` text occurs exactly once in its file.
    - No plant makes a walk run forever: each fault ends in a refusal, a
      wrong value, or an error the suite reports.

Call:
    ``from plants_models_draw import MODELS_DRAW_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

C = "jedimaster/models/colors.py"
D = "jedimaster/models/draw.py"
G = "jedimaster/models/regroup.py"
TR = "tests/test_models_reader.py::"
TT = "tests/test_models_textures.py::"
TD = "tests/test_models_draw.py::"
GLOW_STEPS = TT + "test_each_step_of_the_glow_rule"
REWRITE = TD + "test_the_rewrite_moves_a_later_face_with_the_same_last_texture"

_COLORS = [
    (
        "red-not-widened",
        C,
        "        (red << 3) | (red >> 2),",
        "        (red << 3),",
        (TT + "test_565_colors_widen_to_8_bits",),
    ),
    (
        "green-widened-wrong",
        C,
        "        (green << 2) | (green >> 4),",
        "        (green << 2) | (green >> 3),",
        (TT + "test_565_colors_widen_to_8_bits",),
    ),
    (
        "blue-not-widened",
        C,
        "        (blue << 3) | (blue >> 2),",
        "        (blue << 3),",
        (TT + "test_565_colors_widen_to_8_bits",),
    ),
    (
        "green-six-bits-wrong",
        C,
        "    return (color >> 11) & 31, (color >> 5) & 63, color & 31",
        "    return (color >> 11) & 31, (color >> 6) & 63, color & 31",
        (TT + "test_565_colors_widen_to_8_bits",),
    ),
    (
        "rgb-from-sub-palette-0",
        C,
        "    table = [bytes(rgb8(c)) for c in texture.colors(sub_palette)]",
        "    table = [bytes(rgb8(c)) for c in texture.colors(0)]",
        (TT + "test_top_level_through_a_sub_palette",),
    ),
    (
        "base-sub-palette-always-8",
        C,
        "    return min(BASE_SUB_PALETTE, texture.sub_palettes - 1)",
        "    return BASE_SUB_PALETTE",
        (TT + "test_top_level_through_a_sub_palette",),
    ),
    (
        "dark-limit-inclusive",
        C,
        "    if _distance(color, (0, 0, 0)) < DARK_LIMIT:",
        "    if _distance(color, (0, 0, 0)) <= DARK_LIMIT:",
        (GLOW_STEPS,),
    ),
    (
        "shade-limit-exclusive",
        C,
        "        _distance(color, _five(tables[k][index])) <= SHADE_LIMIT",
        "        _distance(color, _five(tables[k][index])) < SHADE_LIMIT",
        (GLOW_STEPS,),
    ),
    (
        "shade-sub-palettes-short",
        C,
        "SHADED_SUB_PALETTES = range(1, 7)",
        "SHADED_SUB_PALETTES = range(1, 6)",
        (GLOW_STEPS,),
    ),
    (
        "glow-green-six-bits",
        C,
        "    return (color >> 11) & 31, (color >> 6) & 31, color & 31",
        "    return (color >> 11) & 31, (color >> 5) & 31, color & 31",
        (GLOW_STEPS,),
    ),
    (
        "glow-color-sub-palette-0",
        C,
        "    colors = tuple(tables[GLOW_SUB_PALETTE][i] if lit[i] else 0",
        "    colors = tuple(tables[0][i] if lit[i] else 0",
        (GLOW_STEPS,),
    ),
    (
        "glow-when-all-glow",
        C,
        "    if not dark or len(dark) == 256:",
        "    if len(dark) == 256:",
        (TT + "test_no_index_or_every_index_glowing_gives_no_glow",),
    ),
    (
        "glow-when-none-glow",
        C,
        "    if not dark or len(dark) == 256:",
        "    if not dark:",
        (TT + "test_no_index_or_every_index_glowing_gives_no_glow",),
    ),
    (
        "glow-first-none-0",
        C,
        "    first = dark[0] if dark else NO_INDEX",
        "    first = dark[0] if dark else 0",
        (TT + "test_no_index_or_every_index_glowing_gives_no_glow",),
    ),
    (
        "glow-short-palette",
        C,
        "    if texture.sub_palettes <= GLOW_SUB_PALETTE:",
        "    if texture.sub_palettes < GLOW_SUB_PALETTE:",
        (TT + "test_a_short_inline_palette_has_no_glow",),
    ),
    (
        "glow-count-lit",
        C,
        "    return Glow(True, colors, len(dark), first, lit)",
        "    return Glow(True, colors, 256 - len(dark), first, lit)",
        (GLOW_STEPS, TT + "test_glow_fields_of_the_file_sheet"),
    ),
]

_DRAW = [
    (
        "reference-loop-walked",
        D,
        "            if target is None or target in self.active:",
        "            if target is None:",
        (TD + "test_a_reference_chain_that_loops_draws_nothing",),
    ),
    (
        "missing-not-counted",
        D,
        "                self.missing += target is None",
        "                self.missing += 0",
        (TD + "test_name_references_ignore_letter_case_and_follow_chains",),
    ),
    (
        "no-copy-for-children",
        D,
        "            inner = state.copy()",
        "            inner = state",
        (TD + "test_one_copy_of_the_state_for_all_children",),
    ),
    (
        "picked-child-copied",
        D,
        "                self.walk(picked, state)",
        "                self.walk(picked, state.copy())",
        (TD + "test_a_picked_child_shares_the_state_of_the_group",),
    ),
    (
        "level-beyond-picked",
        D,
        "return children[self.lod - 1] if len(children) >= self.lod else None",
        "return children[self.lod - 1] if len(children) > self.lod else None",
        (TD + "test_a_face_group_picks_the_level_or_none",),
    ),
    (
        "switch-child-s",
        D,
        "            return children[min(self.switch, len(children) - 1)]",
        "            return children[min(self.switch + 1, len(children) - 1)]",
        (TD + "test_a_switch_picks_its_child_or_its_last",),
    ),
    (
        "switch-no-last",
        D,
        "            return children[min(self.switch, len(children) - 1)]",
        "            return children[self.switch] if self.switch < len(children) else None",
        (TD + "test_a_switch_picks_its_child_or_its_last",),
    ),
    (
        "empty-switch",
        D,
        "            if not children:\n                return None\n",
        "",
        (TD + "test_a_switch_picks_its_child_or_its_last",),
    ),
    (
        "last-texture-not-kept",
        D,
        "            self.last_texture = number\n",
        "            pass\n",
        (TD + "test_one_copy_of_the_state_for_all_children",),
    ),
    (
        "taken-texture-not-kept",
        D,
        "            state.texture, state.has_texture = self.last_texture, True",
        "            state.texture, state.has_texture = self.last_texture, False",
        (TD + "test_the_last_texture_starts_white_and_a_state_keeps_what_it_took",),
    ),
    (
        "taken-texture-named",
        D,
        "            state.named = False",
        "            state.named = True",
        (TD + "test_the_last_texture_starts_white_and_a_state_keeps_what_it_took",),
    ),
    (
        "coords-not-set",
        D,
        "            state.coords = coord_pairs(node)",
        "            state.coords = None",
        (TD + "test_a_face_takes_its_corners_from_the_state_lists",),
    ),
    (
        "no-extra-normals",
        D,
        "    normals = state.normals if state.normals is not None else extra",
        "    normals = state.normals",
        (TD + "test_normals_stored_after_the_faces_when_no_normal_list",),
    ),
    (
        "negative-index-wraps",
        D,
        "    if values is None or not 0 <= index < len(values):",
        "    if values is None or index >= len(values):",
        (TD + "test_a_value_whose_index_is_outside_its_list_is_bad",),
    ),
    (
        "texture-root-a-component",
        D,
        "        if root is not None and model.nodes[root].type == TEXTURE:",
        "        if False:",
        (TD + "test_the_roots_share_one_state_and_a_texture_root_is_no_component",),
    ),
    (
        "texture-roots-hidden",
        D,
        "        if n != TEXTURE_ROOTS or drawn[n]",
        "        if n != TEXTURE_ROOTS",
        (TD + "test_faces_under_a_texture_root_are_component_minus_1",),
    ),
    (
        "texture-roots-always",
        D,
        "        if n != TEXTURE_ROOTS or drawn[n]",
        "        if True",
        (TD + "test_the_roots_share_one_state_and_a_texture_root_is_no_component",),
    ),
    (
        "roots-own-state",
        D,
        "            walk.walk(root, state)",
        "            walk.walk(root, state.copy())",
        (TD + "test_the_roots_share_one_state_and_a_texture_root_is_no_component",),
    ),
    (
        "empty-node-no-mesh",
        D,
        "        self.meshes += 1",
        "        self.meshes += bool(refs)",
        (TD + "test_a_face_data_node_without_faces_still_counts_as_a_mesh",),
    ),
    (
        "emptied-and-empty-alike",
        D,
        "        if refs is None:",
        "        if not refs:",
        (TD + "test_a_face_data_node_without_faces_still_counts_as_a_mesh",),
    ),
    (
        "walk-limit-off",
        D,
        "        if len(self.active) >= WALK_LIMIT:\n            logger",
        "        if False:\n            logger",
        (TD + "test_a_walk_too_deep_is_cut",),
    ),
    (
        "passes-switch-0-twice",
        D,
        "        (1, switch) for switch in range(1, switches)",
        "        (1, switch) for switch in range(0, switches)",
        (TD + "test_passes_follow_the_most_children_of_groups_and_switches",),
    ),
    (
        "most-children-0",
        D,
        "    return max([1, *counts])",
        "    return max([0, *counts])",
        (TD + "test_passes_follow_the_most_children_of_groups_and_switches",),
    ),
]

_REGROUP = [
    (
        "reference-names-keep-case",
        G,
        "self.by_name.setdefault(node.name.lower(), node.number)",
        "self.by_name.setdefault(node.name, node.number)",
        (TD + "test_name_references_ignore_letter_case_and_follow_chains",),
    ),
    (
        "last-name-wins",
        G,
        "self.by_name.setdefault(node.name.lower(), node.number)",
        "self.by_name[node.name.lower()] = node.number",
        (TD + "test_the_first_node_of_a_name_wins_and_a_reference_stands_in_for_it",),
    ),
    (
        "chain-not-followed",
        G,
        "            if node.type != NAME_REFERENCE:\n",
        "            if True:\n",
        (TD + "test_name_references_ignore_letter_case_and_follow_chains",),
    ),
    (
        "v0-not-rewritten",
        G,
        "REGROUPED_VERSIONS = (0, 1)",
        "REGROUPED_VERSIONS = (1,)",
        (REWRITE + "[0]",),
    ),
    (
        "v2-rewritten",
        G,
        "REGROUPED_VERSIONS = (0, 1)",
        "REGROUPED_VERSIONS = (0, 1, 2)",
        (TD + "test_version_2_is_drawn_as_it_is",),
    ),
    (
        "any-texture-gathered",
        G,
        "            and self.last_texture == self.texture\n",
        "\n",
        (REWRITE,),
    ),
    (
        "edge-count-0-gathered",
        G,
        "            and rewrite.edges[number] > 0\n",
        "            and rewrite.edges[number] >= 0\n",
        (TD + "test_the_rewrite_and_edge_counts",),
    ),
    (
        "taken-from-the-start",
        G,
        "        self.started = False",
        "        self.started = True",
        (REWRITE,),
    ),
    (
        "sweep-ignores-references",
        G,
        '            target = self.rewrite.resolver.resolve(node.reference or b"")',
        "            target = None",
        (REWRITE,),
    ),
    (
        "main-ignores-references",
        G,
        "            if target is not None:\n                self.last_texture = target",
        "            if target is not None:\n                pass",
        (TD + "test_a_reference_sets_the_last_texture_seen",),
    ),
    (
        "whole-group-gathered",
        G,
        "        return self.path[at + 1] if at + 1 < len(self.path) else None",
        "        return self.path[at]",
        (
            TD
            + "test_the_rewrite_leaves_other_levels_alone_and_empties_faces_outside_them",
        ),
    ),
    (
        "left-group-still-holds",
        G,
        "        if self.last_group is None or self.last_group not in self.path:",
        "        if self.last_group is None:",
        (
            TD
            + "test_the_rewrite_leaves_other_levels_alone_and_empties_faces_outside_them",
        ),
    ),
    (
        "own-faces-kept",
        G,
        "        self.lists[first] = sweep.gathered",
        "        self.lists[first] = self.lists[first] + sweep.gathered",
        (TD + "test_the_rewrite_and_edge_counts",),
    ),
    ("gatherer-emptied", G, "        self.emptied.discard(first)\n", "", (REWRITE,)),
    (
        "emptied-gathers-again",
        G,
        "        elif node.type in FACE_TYPES and number not in self.emptied:",
        "        elif node.type in FACE_TYPES:",
        (REWRITE,),
    ),
]


def _plants(rows: list[tuple]) -> list[Plant]:
    return [
        Plant(f"models-{i}", path, old, new, expect)
        for i, path, old, new, expect in rows
    ]


OUTSIDE = (
    TD + "test_the_rewrite_leaves_other_levels_alone_and_empties_faces_outside_them"
)
BEFORE = TD + "test_a_face_before_any_face_group_is_refused"
MOVED = TD + "test_moved_faces_use_the_lists_the_gathering_walk_had"
KEPT = TD + "test_moved_faces_keep_the_lists_current_at_the_gathering_node"
GM = "jedimaster/models/game.py"

# fmt: off
_LISTS = [
    ("outside-faces-kept", G, "            self.lists[first] = []\n            return",
     "            return", (OUTSIDE,)),
    ("no-group-yet-allowed", G, "            if self.last_group is None:\n                raise",
     "            if False:\n                raise", (BEFORE,)),
    ("sweep-lists-unchanged", G,
     "            self.lists = _with_list(self.lists, node.type, number)",
     "            pass", (MOVED,)),
    ("sweep-starts-without-lists", G, "        self.lists = rewrite.current",
     "        self.lists = (None, None, None)", (KEPT,)),
    ("whole-walk-lists-untracked", G,
     "            self.current = _with_list(self.current, node.type, number)",
     "            pass", (KEPT,)),
    ("moved-faces-unbound", D,
     "use = state if lists is None else self._bound(lists, state)", "use = state",
     (MOVED, KEPT)),
    ("bound-coords-as-vectors", D,
     "self.decoded[number] = coord_pairs(node) if kind else vectors(node)",
     "self.decoded[number] = vectors(node)", (MOVED,)),
    ("load-skips-the-rewrite", GM,
     "        if model.version in REGROUPED_VERSIONS:\n            regroup(model)",
     "        if False:\n            regroup(model)", (BEFORE,)),
]
# fmt: on

MODELS_DRAW_PLANTS: list[Plant] = _plants(_COLORS + _DRAW + _REGROUP + _LISTS)
