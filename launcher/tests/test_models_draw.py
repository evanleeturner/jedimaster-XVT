"""The drawing: the walk's state, picks, name references, and the regrouping.

Purpose:
    Prove the drawing walk keeps one state shared by the roots and one
    copy for all of a node's children, takes the last texture when its
    state has none (starting from the built-in white one), resolves name
    references (letter case, chains, a name not found), picks a face
    group's level and a node switch's child as the game does, gives bad
    indices as ``None``, and that the load-time rewrite of version 0 and 1
    models moves faces as the game does while version 2 is left alone.

Flow:
    Build synthetic models with ``optbuilder``; read; draw passes; check
    each component's faces.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_models_draw.py``
"""

from __future__ import annotations

import logging

import pytest
from optbuilder import build_opt
from optbuilder import Face
from optbuilder import faces
from optbuilder import N
from optbuilder import palette
from optbuilder import Tex
from optbuilder import uvs
from optbuilder import vecs

from jedimaster.models import draw_model
from jedimaster.models import draw_pass
from jedimaster.models import ModelFormatError
from jedimaster.models import passes
from jedimaster.models import read_opt
from jedimaster.models import regroup
from jedimaster.models import Resolver
from jedimaster.models.draw import most_children
from jedimaster.models.game import load_model
from jedimaster.models.game import ModelEntry
from jedimaster.models.render import draw_lines
from jedimaster.models.render import face_line

logger = logging.getLogger(__name__)

POINTS = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (1.0, 1.0, 0.0)]
BLOCK = palette()


def tex(name: bytes) -> N:
    """Return a named 1x1 texture node carrying its own palette."""
    return N(20, name, payload=Tex(1, 1, b"\0", palette=BLOCK))


def ref(name: bytes) -> N:
    """Return a name reference node naming ``name``."""
    return N(7, payload=name + b"\0")


def face(tag: float, version: int = 1, edges: int = 1, extra: int = 0) -> N:
    """Return a face data node of one triangle whose face normal is (tag, 0, 0)."""
    record = Face(
        (0, 1, 2), coords=(0, 1, 2), normals=(2, 1, 0), normal=(tag, 0.0, 0.0)
    )
    extras = [(0.0, 0.0, float(k)) for k in range(extra)]
    return N(1, count=1, payload=faces(version, [record], edges=edges, extra=extras))


def lists(*, normals: bool = True) -> list[N]:
    """Return a vertices, a coordinates and (when asked) a normals node."""
    out = [
        N(3, count=4, payload=vecs(POINTS)),
        N(13, count=4, payload=uvs([(0.0, 0.0), (1.0, 0.0), (0.0, 1.0), (1.0, 1.0)])),
    ]
    if normals:
        out.append(
            N(11, count=3, payload=vecs([(1.0, 0, 0), (0, 1.0, 0), (0, 0, 1.0)]))
        )
    return out


def drawn(model, lod: int = 1, switch: int = 0) -> dict[int, list[tuple]]:
    """Return each component's faces as (face normal x, texture, named)."""
    result = draw_pass(model, lod, switch)
    return {
        c.number: [(f.normal[0], f.texture, f.named) for f in c.faces]
        for c in result.components
    }


def model_of(roots: list, version: int = 2):
    """Return the model read from ``roots``."""
    return read_opt(build_opt(roots, version=version))


def test_a_face_takes_its_corners_from_the_state_lists():
    model = model_of([N(0, children=[*lists(), face(7.0)])])
    result = draw_pass(model, 1, 0)
    (component,) = result.components
    (one,) = component.faces
    assert component.number == 0 and result.meshes == 1
    assert [c.position for c in one.corners] == POINTS[:3]
    assert [c.uv for c in one.corners] == [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)]
    assert [c.normal for c in one.corners] == [(0, 0, 1.0), (0, 1.0, 0), (1.0, 0, 0)]
    assert one.normal == (7.0, 0.0, 0.0)
    assert (one.texture, one.named, one.node, one.source) == (None, False, 4, 4)
    line = face_line(one, model)
    assert line.startswith(
        "face tex=white corners=3 p=0,0,0;1,0,0;0,1,0 uv=0,0;1,0;0,1"
    )
    assert line.endswith(" n=0,0,1;0,1,0;1,0,0 fn=7,0,0")


def test_normals_stored_after_the_faces_when_no_normal_list():
    model = model_of([N(0, children=[*lists(normals=False), face(1.0, extra=4)])])
    (one,) = draw_pass(model, 1, 0).components[0].faces
    assert [c.normal for c in one.corners] == [(0, 0, 2.0), (0, 0, 1.0), (0, 0, 0)]


def test_a_value_whose_index_is_outside_its_list_is_bad():
    record = Face((0, 9, 3, 1), coords=(0, -1, 3, 4), normals=(0, 1, 2, 3))
    four = N(1, count=1, payload=faces(1, [record]))
    model = model_of([N(0, children=[*lists(), four])])
    (one,) = draw_pass(model, 1, 0).components[0].faces
    assert [c.position for c in one.corners] == [POINTS[0], None, POINTS[3], POINTS[1]]
    assert [c.uv for c in one.corners][1:] == [None, (1.0, 1.0), None]
    assert one.corners[3].normal is None
    line = face_line(one, model)
    assert " corners=4 p=0,0,0;bad;1,1,0;1,0,0 uv=0,0;bad;1,1;bad " in line
    bare = model_of([N(0, children=[face(1.0)])])
    (lone,) = draw_pass(bare, 1, 0).components[0].faces
    assert all(c.position is None and c.uv is None for c in lone.corners)


def test_one_copy_of_the_state_for_all_children():
    sub = N(0, children=[tex(b"B"), face(2.0)])
    top = N(0, children=[*lists(), tex(b"A"), face(1.0), sub, face(3.0)])
    model = model_of([top, N(0, children=[*lists(), face(4.0)])])
    assert drawn(model) == {
        0: [(1.0, 4, True), (2.0, 7, True), (3.0, 4, True)],
        1: [(4.0, 7, False)],
    }


def test_the_roots_share_one_state_and_a_texture_root_is_no_component():
    model = model_of([tex(b"R"), N(0, children=[*lists(), face(1.0)]), tex(b"S")])
    result = draw_pass(model, 1, 0)
    assert [c.number for c in result.components] == [0]
    assert drawn(model) == {0: [(1.0, 0, True)]}


def test_faces_under_a_texture_root_are_component_minus_1():
    under = N(
        20,
        b"T",
        children=[*lists(), face(1.0)],
        payload=Tex(1, 1, b"\0", palette=BLOCK),
    )
    model = model_of([under, N(0, children=[*lists(), face(2.0)])])
    assert drawn(model) == {-1: [(1.0, 0, True)], 0: [(2.0, 0, True)]}
    lines = draw_lines_of(model)
    assert lines[1].startswith("model ") and " components=1 " in lines[1]
    assert lines[3] == "component -1 faces=1" and lines[5] == "component 0 faces=1"


def draw_lines_of(model) -> list[str]:
    """Return the draw sheet lines of ``model`` as one loaded entry."""
    entry = ModelEntry("ivfiles\\x.opt", "loaded", None, "ivfiles/x.opt", model, None)
    return ["", *draw_lines(entry)]


def test_the_last_texture_starts_white_and_a_state_keeps_what_it_took():
    early = N(0, children=[*lists(), face(1.0), N(0, children=[tex(b"B")]), face(2.0)])
    model = model_of([early, N(0, children=[*lists(), face(3.0), face(4.0)])])
    assert drawn(model) == {
        0: [(1.0, None, False), (2.0, None, False)],
        1: [(3.0, 6, False), (4.0, 6, False)],
    }


def test_name_references_ignore_letter_case_and_follow_chains():
    texture = tex(b"TexA")
    alias = N(7, b"Alias", payload=b"TEXA\0")
    group = N(
        0, children=[*lists(), ref(b"alias"), face(1.0), ref(b"Nothing"), face(2.0)]
    )
    model = model_of([texture, alias, group])
    assert drawn(model) == {0: [], 1: [(1.0, 0, True), (2.0, 0, True)]}
    result = draw_pass(model, 1, 0)
    assert result.missing == 1
    resolver = Resolver(model)
    assert resolver.resolve(b"tExA") == 0 and resolver.resolve(b"ALIAS") == 0
    assert resolver.resolve(b"Nothing") is None and resolver.texture(b"x") is None


def test_the_first_node_of_a_name_wins_and_a_reference_stands_in_for_it():
    first = N(0, b"Part", children=[*lists(), face(1.0)])
    model = model_of(
        [N(0, children=[first]), N(0, b"part"), N(0, children=[ref(b"PART")])]
    )
    assert drawn(model) == {0: [(1.0, None, False)], 1: [], 2: [(1.0, None, False)]}


def test_a_reference_chain_that_loops_draws_nothing():
    loop = N(0, b"Loop", children=[*lists(), face(1.0)])
    loop.children.append(ref(b"loop"))
    a = N(7, b"A", payload=b"B\0")
    b = N(7, b"B", payload=b"A\0")
    model = model_of([loop, a, b, N(0, children=[ref(b"A")])])
    assert drawn(model) == {0: [(1.0, None, False)], 1: [], 2: [], 3: []}
    assert Resolver(model).resolve(b"A") is None


def levels() -> N:
    """Return a face group of two levels, each drawing one face."""
    return N(21, count=2, payload=b"\0" * 8, children=[
        N(0, children=[face(1.0)]), N(0, children=[face(2.0)]),
    ])  # fmt: skip


def test_a_face_group_picks_the_level_or_none():
    model = model_of([N(0, children=[*lists(), levels()])])
    assert drawn(model, lod=1) == {0: [(1.0, None, False)]}
    assert drawn(model, lod=2) == {0: [(2.0, None, False)]}
    assert drawn(model, lod=3) == {0: []}


def test_a_switch_picks_its_child_or_its_last():
    switch = N(24, children=[tex(b"S0"), tex(b"S1"), tex(b"S2")])
    model = model_of([N(0, children=[*lists(), switch, face(1.0)])])
    assert [drawn(model, switch=s)[0][0][1] for s in range(5)] == [5, 6, 7, 7, 7]
    empty = model_of([N(0, children=[*lists(), N(24), face(1.0)])])
    assert drawn(empty, switch=2) == {0: [(1.0, None, False)]}


def test_a_picked_child_shares_the_state_of_the_group():
    group = N(21, count=1, payload=b"\0" * 4, children=[tex(b"L")])
    model = model_of([N(0, children=[*lists(), group, face(1.0)])])
    assert drawn(model) == {0: [(1.0, 5, True)]}
    slot = N(
        21, count=1, payload=b"\0" * 4, children=[None, N(0, children=[face(1.0)])]
    )
    model = model_of([N(0, children=[*lists(), slot])])
    assert drawn(model, lod=1) == {0: []}
    assert drawn(model, lod=2) == {0: [(1.0, None, False)]}


def test_passes_follow_the_most_children_of_groups_and_switches():
    switch = N(24, children=[N(0), N(0)])
    group = N(21, count=3, payload=b"\0" * 12, children=[N(0), N(0), N(0)])
    model = model_of([N(0, children=[group, switch])])
    assert passes(model) == [(1, 0), (2, 0), (3, 0), (1, 1)]
    assert most_children(model_of([N(0)]), 24) == 1
    assert passes(model_of([N(0)])) == [(1, 0)]
    assert [p.lod for p in draw_model(model)] == [1, 2, 3, 1]


def regroup_roots(version: int) -> list[N]:
    """Return a model whose second level gathers a face that follows a reference.

    Level 1: texture X, then face 0. Level 2: face 1 (last texture seen
    there is X), texture Y, face 2, a reference to X, face 3.
    """
    level1 = N(0, children=[tex(b"X"), face(0.0, version)])
    level2 = N(0, children=[
        face(1.0, version), tex(b"Y"), face(2.0, version),
        ref(b"x"), face(3.0, version),
    ])  # fmt: skip
    group = N(21, count=2, payload=b"\0" * 8, children=[level1, level2])
    return [tex(b"R"), N(0, children=[*lists(), group])]


@pytest.mark.parametrize("version", (0, 1))
def test_the_rewrite_moves_a_later_face_with_the_same_last_texture(version):
    model = model_of(regroup_roots(version), version=version)
    numbers = {n.normal[0]: n.node for n in draw_pass(model, 2, 0).components[0].faces}
    assert drawn(model, lod=2) == {0: [(1.0, 0, True), (3.0, 0, True), (2.0, 11, True)]}
    assert numbers[3.0] == numbers[1.0]
    assert drawn(model, lod=1) == {0: [(0.0, 7, True)]}
    lists_after = regroup(model)
    gathered = lists_after[numbers[1.0]]
    assert [f.source for f in gathered] == [numbers[1.0], 14]
    assert gathered[0].lists is None and gathered[1].lists == (2, 3, 4)
    assert 14 not in lists_after
    result = draw_pass(model, 2, 0)
    assert result.meshes == 2


def test_version_2_is_drawn_as_it_is():
    model = model_of(regroup_roots(2), version=2)
    assert drawn(model, lod=2) == {0: [(1.0, 0, True), (2.0, 11, True), (3.0, 7, True)]}
    assert draw_pass(model, 2, 0).meshes == 3


def test_the_rewrite_and_edge_counts():
    level = N(0, children=[face(1.0, edges=0), face(2.0), face(3.0, edges=0)])
    group = N(21, count=1, payload=b"\0" * 4, children=[level])
    model = model_of([N(0, children=[*lists(), group])], version=1)
    assert drawn(model) == {0: [(2.0, None, False)]}
    result = draw_pass(model, 1, 0)
    assert [(f.node, f.source) for f in result.components[0].faces] == [(6, 7)]
    assert result.meshes == 2
    assert regroup(model) == {6: ((7, 0, (1, 2, 3)),), 8: ()}


def test_the_rewrite_leaves_other_levels_alone_and_empties_faces_outside_them():
    level1 = N(0, children=[face(1.0)])
    level2 = N(0, children=[face(2.0)])
    group = N(21, count=2, payload=b"\0" * 8, children=[level1, level2])
    model = model_of([N(0, children=[*lists(), group, face(5.0)])], version=1)
    assert regroup(model) == {6: ((6, 0, None),), 8: ((8, 0, None),), 9: ()}
    assert drawn(model, lod=2) == {0: [(2.0, None, False)]}
    assert draw_pass(model, 2, 0).meshes == 2
    as_is = model_of([N(0, children=[*lists(), group, face(5.0)])])
    assert drawn(as_is, lod=2) == {0: [(2.0, None, False), (5.0, None, False)]}


def test_a_face_before_any_face_group_is_refused(tmp_path):
    group = N(21, count=1, payload=b"\0" * 4, children=[face(1.0)])
    roots = [N(0, children=[*lists(), face(0.0)]), group]
    with pytest.raises(ModelFormatError, match="face data node 4 comes before any"):
        regroup(model_of(roots, version=1))
    assert len(regroup(model_of(roots))) == 2
    path = tmp_path / "x.opt"
    path.write_bytes(build_opt(roots, version=1))
    entry = load_model("ivfiles\\x.opt", path, "ivfiles/x.opt")
    assert entry.status == "not_loaded" and "before any face group" in str(entry.error)
    path.write_bytes(build_opt(roots, version=2))
    assert load_model("ivfiles\\x.opt", path, "ivfiles/x.opt").status == "loaded"


def test_moved_faces_use_the_lists_the_gathering_walk_had():
    other = [(5.0, 5.0, 5.0), (6.0, 6.0, 6.0), (7.0, 7.0, 7.0)]
    later = [
        N(3, count=3, payload=vecs(other)),
        N(13, count=3, payload=uvs([(0.5, 0.5)] * 3)),
    ]
    level = N(0, children=[face(1.0), *later, face(2.0, extra=3)])
    group = N(21, count=1, payload=b"\0" * 4, children=[level])
    model = model_of([N(0, children=[*lists(normals=False), group])], version=1)
    first, moved = draw_pass(model, 1, 0).components[0].faces
    assert [c.position for c in first.corners] == POINTS[:3]
    assert [c.position for c in moved.corners] == other
    assert [c.uv for c in moved.corners] == [(0.5, 0.5)] * 3
    assert [c.normal for c in moved.corners] == [(0, 0, 2.0), (0, 0, 1.0), (0, 0, 0)]
    assert moved.node == first.node and moved.source != first.source


def test_moved_faces_keep_the_lists_current_at_the_gathering_node():
    normals = N(11, count=3, payload=vecs([(1.0, 0.0, 0.0)] * 3))
    level = N(0, children=[face(1.0), normals, face(2.0)])
    group = N(21, count=1, payload=b"\0" * 4, children=[level])
    model = model_of([N(0, children=[*lists(normals=False), group])], version=1)
    _, moved = draw_pass(model, 1, 0).components[0].faces
    assert [c.normal for c in moved.corners] == [(1.0, 0.0, 0.0)] * 3
    assert [c.position for c in moved.corners] == POINTS[:3]
    assert [c.uv for c in moved.corners] == [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)]


def test_a_face_data_node_without_faces_still_counts_as_a_mesh():
    empty = N(1, count=0, payload=faces(1, [], edges=0))
    level = N(0, children=[face(1.0), empty])
    group = N(21, count=1, payload=b"\0" * 4, children=[level])
    model = model_of([N(0, children=[*lists(), group])])
    result = draw_pass(model, 1, 0)
    assert result.meshes == 2
    assert drawn(model) == {0: [(1.0, None, False)]}


def test_a_reference_sets_the_last_texture_seen():
    level = N(0, children=[tex(b"A"), face(1.0), ref(b"B"), face(2.0)])
    group = N(21, count=1, payload=b"\0" * 4, children=[level])
    model = model_of([tex(b"B"), N(0, children=[*lists(), group])], version=1)
    assert drawn(model) == {0: [(1.0, 7, True), (2.0, 0, True)]}
    assert regroup(model) == {8: ((8, 0, None),), 10: ((10, 0, None),)}


def test_version_0_normal_indices_are_the_vertex_indices():
    record = Face((0, 1, 2), coords=(2, 1, 0), normal=(1.0, 0.0, 0.0))
    node = N(1, count=1, payload=faces(0, [record]))
    group = N(21, count=1, payload=b"\0" * 4, children=[node])
    model = model_of([N(0, children=[*lists(), group])], version=0)
    (one,) = draw_pass(model, 1, 0).components[0].faces
    assert [c.normal for c in one.corners] == [(1.0, 0, 0), (0, 1.0, 0), (0, 0, 1.0)]
    assert [c.uv for c in one.corners] == [(0.0, 1.0), (1.0, 0.0), (0.0, 0.0)]


def test_face_lines_name_their_texture():
    unnamed = N(20, payload=Tex(1, 1, b"\0", palette=BLOCK))
    named = tex(b"Some name")
    group = N(0, children=[*lists(), face(1.0), named, face(2.0), unnamed, face(3.0)])
    model = model_of([group])
    lines = [face_line(f, model) for f in draw_pass(model, 1, 0).components[0].faces]
    assert [line.split(" corners=")[0] for line in lines] == [
        "face tex=white",
        'face tex="Some name"',
        "face tex=-",
    ]


def test_a_walk_too_deep_is_cut():
    count = 600
    roots = [
        N(0, f"G{k}".encode(), children=[ref(f"G{k + 1}".encode())])
        for k in range(count)
    ]
    roots.append(N(0, f"G{count}".encode(), children=[*lists(), face(1.0)]))
    model = model_of(roots)
    result = drawn(model)
    assert result[0] == [] and result[count - 300] == [(1.0, None, False)]
    assert result[count] == [(1.0, None, False)]
