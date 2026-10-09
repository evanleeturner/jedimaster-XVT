"""The model reader: markers, the body, addresses, nodes, payloads, refusals.

Purpose:
    Prove ``read_opt`` reads each version marker, checks the body's size,
    turns links into body offsets from the base address, numbers the
    nodes in first-visit order (a node reached twice is one node), keeps
    each payload as the game does (cut payloads read as zeros, face data
    sized by the vertex count and normals met before it), and refuses,
    naming the rule, exactly what the game refuses.

Flow:
    Build synthetic files with ``optbuilder``; read; inspect or expect a
    ``ModelFormatError``.

Invariants:
    - No game data.

Call:
    ``pytest tests/test_models_reader.py``
"""

from __future__ import annotations

import logging
import struct

import pytest
from optbuilder import build_opt
from optbuilder import descriptor
from optbuilder import Face
from optbuilder import faces
from optbuilder import N
from optbuilder import vecs

from jedimaster.models import ModelFormatError
from jedimaster.models import read_opt
from jedimaster.models import reader
from jedimaster.models.render import file_body_lines
from jedimaster.models.render import hex64

logger = logging.getLogger(__name__)

TRI = [Face((0, 1, 2))]


def leaf(kind: int = 0) -> N:
    """Return a node with no payload and no children."""
    return N(kind)


def verts(count: int) -> N:
    """Return a vertices node of ``count`` distinct vectors."""
    return N(3, count=count, payload=vecs([(i, -i, 2 * i) for i in range(count)]))


def face_node(version: int = 1, extra: int = 0) -> N:
    """Return a face data node of one triangle with ``extra`` stored normals."""
    payload = faces(version, TRI, extra=[(0.0, 0.0, 1.0)] * extra)
    return N(1, count=1, payload=payload)


@pytest.mark.parametrize("version", (0, 1, 2))
def test_each_version_marker(version):
    model = read_opt(build_opt([leaf()], version=version, reserved=513))
    assert model.version == version
    assert model.reserved == 513
    assert model.base == 0x10000
    assert model.size == 14 + 4 + 24


@pytest.mark.parametrize("marker", (0, -3, -100))
def test_other_markers_refused(marker):
    with pytest.raises(ModelFormatError, match="marker"):
        read_opt(build_opt([leaf()], marker=marker))


def test_version_0_marker_is_the_size():
    data = build_opt([leaf()], version=0)
    assert struct.unpack_from("<i", data)[0] == len(data) - 4
    assert read_opt(data).version == 0
    with pytest.raises(ModelFormatError, match="remain"):
        read_opt(data + b"\0")


def test_short_files_refused():
    with pytest.raises(ModelFormatError, match="4-byte marker"):
        read_opt(b"\x10\0")
    with pytest.raises(ModelFormatError, match="marker and size"):
        read_opt(b"\xff\xff\xff\xff\x10")


def test_body_size_limits():
    assert read_opt(build_opt([])).size == 14
    with pytest.raises(ModelFormatError, match="body size 13 is not 14"):
        read_opt(build_opt([], cut=1))
    big = reader.BODY_MAX + 1
    with pytest.raises(ModelFormatError, match=f"body size {big} is not 14 to"):
        read_opt(build_opt([], size=big))
    assert reader.BODY_MAX == 128 * 1024 * 1024


def test_body_size_must_be_what_remains():
    with pytest.raises(ModelFormatError, match="remain"):
        read_opt(build_opt([leaf()], tail=b"xy"))
    data = build_opt([leaf()])
    with pytest.raises(ModelFormatError, match="remain"):
        read_opt(data[:-1])


def test_links_are_addresses_from_the_base():
    texture_like = N(0, b"Abc", children=[verts(2)])
    for base in (0x400000, 0x7FFF0000):
        model = read_opt(build_opt([texture_like], base=base))
        assert model.base == base
        assert model.nodes[0].name == b"Abc"
        assert model.nodes[1].count == 2


def test_base_plus_size_may_not_pass_the_address_space():
    data = build_opt([leaf()], base=0xFFFFFFFF - 42)
    assert read_opt(data).size == 42
    with pytest.raises(ModelFormatError, match="2\\^32"):
        read_opt(build_opt([leaf()], base=0xFFFFFFFF - 41))


@pytest.mark.parametrize("delta", (-4, -1, 42))
def test_root_link_outside_the_body_refused(delta):
    with pytest.raises(ModelFormatError, match="not inside the body"):
        read_opt(build_opt([leaf()], root_links=[0x10000 + delta]))


def test_node_must_fit_in_the_body():
    with pytest.raises(ModelFormatError, match="runs past the body"):
        read_opt(build_opt([leaf()], root_links=[0x10000 + 42 - 23]))


def test_root_count_limits():
    one = leaf()
    many = build_opt([one] * reader.MAX_ROOTS)
    assert len(read_opt(many).roots) == 65536
    with pytest.raises(ModelFormatError, match="root count 65537"):
        read_opt(build_opt([one], root_count=65537))
    with pytest.raises(ModelFormatError, match="root count -1"):
        read_opt(build_opt([one], root_count=-1))


def test_root_table_must_lie_in_the_body():
    with pytest.raises(ModelFormatError, match="root table"):
        read_opt(build_opt([leaf()], root_table=0x10000 + 40))
    with pytest.raises(ModelFormatError, match="root table link"):
        read_opt(build_opt([leaf()], root_table=0))
    assert read_opt(build_opt([], root_table=0)).roots == ()


def test_empty_root_and_child_slots():
    model = read_opt(build_opt([None, N(0, children=[None, leaf(), None])]))
    assert model.roots == (None, 0)
    assert model.nodes[0].children == (None, 1, None)
    assert file_body_lines(model)[:3] == [
        "root 0 node=-",
        "root 1 node=0",
        "node 0 type=0 name=- children=-,1,- payload_count=1",
    ]


def test_first_visit_numbering_with_a_node_reached_twice():
    shared = N(0, b"Shared", children=[leaf(3 - 3)])
    a = N(0, b"A", children=[shared, leaf()])
    b = N(0, b"B", children=[leaf(), shared, shared])
    model = read_opt(build_opt([a, b, shared]))
    names = [n.name for n in model.nodes]
    assert names == [b"A", b"Shared", None, None, b"B", None]
    assert model.nodes[0].children == (1, 3)
    assert model.nodes[4].children == (5, 1, 1)
    assert model.roots == (0, 4, 1)


def test_a_cycle_is_refused():
    a = N(0, b"A")
    b = N(0, b"B", children=[a])
    a.children = [N(0, children=[b])]
    with pytest.raises(ModelFormatError, match="own ancestor"):
        read_opt(build_opt([a]))


def test_nesting_limit():
    def chain(levels: int) -> N:
        top = node = N(0)
        for _ in range(levels - 1):
            child = N(0)
            node.children = [child]
            node = child
        return top

    assert len(read_opt(build_opt([chain(256)])).nodes) == 256
    with pytest.raises(ModelFormatError, match="deeper than 256"):
        read_opt(build_opt([chain(257)]))


def test_node_count_limit(monkeypatch):
    monkeypatch.setattr(reader, "MAX_NODES", 4)
    assert (
        len(read_opt(build_opt([N(0, children=[leaf(), leaf(), leaf()])])).nodes) == 4
    )
    with pytest.raises(ModelFormatError, match="more than 4 nodes"):
        read_opt(build_opt([N(0, children=[leaf(), leaf(), leaf(), leaf()])]))


def test_the_limits_are_the_games():
    assert reader.MAX_NODES == 65536
    assert reader.MAX_DEPTH == 256
    assert reader.MAX_PAYLOAD_COUNT == 1_000_000


def test_child_count_limits():
    one = leaf()
    model = read_opt(build_opt([N(0, children=[one] * 65536)]))
    assert len(model.nodes[0].children) == 65536 and len(model.nodes) == 2
    for count in (65537, -1):
        bad = N(0, child_count=count, table_link=0x10000)
        with pytest.raises(ModelFormatError, match=f"child count {count}"):
            read_opt(build_opt([bad]))


def test_child_table_must_lie_in_the_body():
    with pytest.raises(ModelFormatError, match="child table"):
        read_opt(build_opt([N(0, child_count=2, table_link=0x10000 + 40)]))
    with pytest.raises(ModelFormatError, match="child table"):
        read_opt(build_opt([N(0, child_count=1, table_link=0)]))
    assert (
        read_opt(build_opt([N(0, child_count=0, table_link=7)])).nodes[0].children == ()
    )


def test_payload_count_limits():
    top = N(25, count=1_000_000, payload=descriptor(1, 2, 3))
    assert read_opt(build_opt([top])).nodes[0].count == 1_000_000
    for count in (1_000_001, -1):
        bad = N(25, count=count, payload=descriptor(1, 2, 3))
        with pytest.raises(ModelFormatError, match=f"payload count {count}"):
            read_opt(build_opt([bad]))


def test_payload_link_rules():
    with pytest.raises(ModelFormatError, match="payload link 0x0"):
        read_opt(build_opt([N(3, count=0, payload_link=0)]))
    with pytest.raises(ModelFormatError, match="payload link 0x0"):
        read_opt(build_opt([N(3, count=0, payload_link=0)], base=0))
    with pytest.raises(ModelFormatError, match="payload link"):
        read_opt(build_opt([N(3, count=1, payload_link=0x20000)]))
    ignored = N(0, count=-5, payload_link=0xDEADBEEF)
    model = read_opt(build_opt([ignored, N(24, payload_link=3), N(-1)]))
    assert [n.payload for n in model.nodes] == [b"", b"", b""]
    assert [n.count for n in model.nodes] == [-5, 1, 1]
    assert model.nodes[2].type == -1


def test_names_must_end_inside_the_body():
    last = N(3, count=0, payload=b"AB")
    named = N(0, name_link=0x10000 + 14 + 8 + 48)
    with pytest.raises(ModelFormatError, match="no 0 byte"):
        read_opt(build_opt([named, last]))
    with pytest.raises(ModelFormatError, match="name link"):
        read_opt(build_opt([N(0, name_link=0x50)]))


def test_name_reference_keeps_count_0_and_reads_its_name():
    ref = N(7, count=9, payload=b"Tex00001\0junk")
    model = read_opt(build_opt([ref]))
    assert model.nodes[0].count == 0
    assert model.nodes[0].reference == b"Tex00001"
    cut = read_opt(build_opt([N(7, count=1, payload=b"Abc")])).nodes[0]
    assert cut.reference == b"Abc"


def test_fixed_and_counted_payload_sizes():
    sizes = {2: 48, 4: 12, 5: 36, 6: 12, 19: 12, 22: 16, 23: 48, 25: 72}
    roots = [N(kind, count=3, payload=bytes(range(80))) for kind in sizes]
    counted = {3: 12, 9: 56, 11: 12, 13: 8, 21: 4}
    roots += [N(kind, count=2, payload=bytes(range(120))) for kind in counted]
    model = read_opt(build_opt(roots))
    got = [len(n.payload) for n in model.nodes]
    assert got == list(sizes.values()) + [2 * size for size in counted.values()]
    assert model.nodes[0].payload == bytes(range(48))


def test_a_payload_cut_by_the_body_end_reads_as_zeros():
    data = build_opt([verts(2)], cut=8)
    node = read_opt(data).nodes[0]
    expected = vecs([(0, 0, 0), (1, -1, 2)])[:16] + bytes(8)
    assert node.payload == expected


@pytest.mark.parametrize("version", (0, 1, 2))
def test_face_payload_sized_by_version(version):
    model = read_opt(build_opt([face_node(version)], version=version))
    size = 48 if version == 0 else 64
    assert len(model.nodes[0].payload) == 4 + size + 12 + 24
    assert model.nodes[0].extra_normals == 0


def test_extra_normals_follow_the_last_vertex_count():
    group = N(0, children=[verts(3), face_node(extra=3)])
    model = read_opt(build_opt([group]))
    assert model.nodes[2].extra_normals == 3
    assert len(model.nodes[2].payload) == 4 + 100 + 36


def test_the_context_rule_that_sizes_extra_normals():
    inner = N(0, children=[verts(5), face_node(extra=5)])
    after_inner = face_node(extra=2)
    normals = N(11, count=1, payload=vecs([(0, 0, 1)]))
    with_normals = N(0, children=[normals, face_node()])
    after_normals = face_node(extra=2)
    top = N(0, children=[verts(2), inner, after_inner, with_normals, after_normals])
    model = read_opt(build_opt([verts(4), face_node(extra=4), top]))
    extras = [n.extra_normals for n in model.nodes if n.type == 1]
    assert extras == [4, 5, 2, 0, 2]


def test_a_node_reached_again_keeps_its_first_size():
    shared = face_node(extra=2)
    first = N(0, children=[verts(2), shared])
    second = N(0, children=[verts(6), shared, face_node(extra=6)])
    model = read_opt(build_opt([first, second]))
    extras = [(n.number, n.extra_normals) for n in model.nodes if n.type == 1]
    assert extras == [(2, 2), (5, 6)]


def test_extra_normals_read_as_zero_past_the_body():
    group = N(0, children=[verts(2), face_node(extra=0)])
    node = read_opt(build_opt([group])).nodes[2]
    assert node.extra_normals == 2
    assert node.payload[-24:] == bytes(24)
    assert hex64(node.payload[-24:]) == hex64(bytes(24))
