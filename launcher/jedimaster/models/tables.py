"""The game's two object tables: each object type's record, each craft's type.

Purpose:
    Hold, as data, the record of each of the 201 object types (0 to 200)
    and the object type of each of the 96 craft types (0 to 95). Neither
    table is in any file of the game: they are the game program's tables,
    as printed by the engine's opt_dump tool, copied here from that
    printout. With the icon tables of ``jedimaster.icons.tables``, the
    strings.txt tables of ``jedimaster.text.tables``, the text colors of
    ``jedimaster.fonts.colors`` and the pilot layout of
    ``jedimaster.pilot.layout``, they are the only game data in this
    package's code.

Flow:
    ``OBJECT_TYPES[t]`` is object type ``t``'s record; ``CRAFT_OBJECTS[c]``
    is craft type ``c``'s object type. ``objects`` reads them; nothing
    here computes.

Invariants:
    - A record's fields are the printout's: record flags, asset flags,
      model index, texture group (the spec list, 0 to 2) and resource
      index (the line of that list).
    - A craft's object type is copied as printed, even where it lies
      beyond the object table (craft type 88).

Call:
    ``OBJECT_TYPES[CRAFT_OBJECTS[craft_type]].resource_index``
"""

from __future__ import annotations

import logging
from typing import NamedTuple

logger = logging.getLogger(__name__)


class ObjectType(NamedTuple):
    """One object type's record: two flag bytes, the model index, its list line."""

    record_flags: int
    asset_flags: int
    model_index: int
    texture_group: int
    resource_index: int


# fmt: off
OBJECT_TYPES: tuple[ObjectType, ...] = (
    ObjectType(0x00, 0x00, 255, 2,   0),  # 0
    ObjectType(0x03, 0x01,   0, 0,   0),  # 1
    ObjectType(0x03, 0x01,   1, 0,   1),  # 2
    ObjectType(0x03, 0x01,   2, 0,   2),  # 3
    ObjectType(0x03, 0x01,   3, 0,   3),  # 4
    ObjectType(0x03, 0x01,   4, 0,   4),  # 5
    ObjectType(0x03, 0x01,   5, 0,   5),  # 6
    ObjectType(0x03, 0x01,   6, 0,   6),  # 7
    ObjectType(0x03, 0x01,   7, 0,   7),  # 8
    ObjectType(0x03, 0x01,   8, 0,   8),  # 9
    ObjectType(0x00, 0x01,   9, 2,   0),  # 10
    ObjectType(0x00, 0x01,  10, 2,   0),  # 11
    ObjectType(0x03, 0x01,  11, 1,  15),  # 12
    ObjectType(0x03, 0x01,  12, 1,   9),  # 13
    ObjectType(0x03, 0x01,  13, 0,   9),  # 14
    ObjectType(0x03, 0x01,  14, 1,  10),  # 15
    ObjectType(0x03, 0x21,  15, 0,  10),  # 16
    ObjectType(0x03, 0x21,  16, 0,  11),  # 17
    ObjectType(0x03, 0x21,  17, 0,  12),  # 18
    ObjectType(0x03, 0x21,  18, 0,  13),  # 19
    ObjectType(0x03, 0x21,  19, 1,  13),  # 20
    ObjectType(0x03, 0x21,  20, 0,  14),  # 21
    ObjectType(0x03, 0x21,  21, 0,  15),  # 22
    ObjectType(0x03, 0x21,  22, 1,   7),  # 23
    ObjectType(0x03, 0x21,  23, 0,  16),  # 24
    ObjectType(0x03, 0x21,  24, 1,  14),  # 25
    ObjectType(0x03, 0x01,  25, 0,  17),  # 26
    ObjectType(0x03, 0x01,  26, 0,  18),  # 27
    ObjectType(0x03, 0x01,  27, 0,  19),  # 28
    ObjectType(0x03, 0x01,  28, 0,  20),  # 29
    ObjectType(0x03, 0x01,  29, 0,  21),  # 30
    ObjectType(0x00, 0x01, 255, 2,   0),  # 31
    ObjectType(0x03, 0x21,  31, 0,  22),  # 32
    ObjectType(0x03, 0x21,  32, 0,  23),  # 33
    ObjectType(0x03, 0x21,  33, 0,  24),  # 34
    ObjectType(0x03, 0x21,  34, 0,  74),  # 35
    ObjectType(0x03, 0x21,  35, 0,  88),  # 36
    ObjectType(0x03, 0x21,  36, 1,  11),  # 37
    ObjectType(0x03, 0x21,  37, 0,  25),  # 38
    ObjectType(0x00, 0x01, 255, 2,   0),  # 39
    ObjectType(0x03, 0x21,  39, 0,  26),  # 40
    ObjectType(0x03, 0x21,  40, 0,  27),  # 41
    ObjectType(0x03, 0x21,  41, 0,  28),  # 42
    ObjectType(0x03, 0x21,  42, 0,  73),  # 43
    ObjectType(0x03, 0x21,  43, 1,  12),  # 44
    ObjectType(0x03, 0x21,  44, 1,   4),  # 45
    ObjectType(0x03, 0x21,  45, 1,   5),  # 46
    ObjectType(0x03, 0x21,  46, 0,  29),  # 47
    ObjectType(0x03, 0x21,  47, 1,   6),  # 48
    ObjectType(0x03, 0x21,  48, 0,  71),  # 49
    ObjectType(0x03, 0x21,  49, 0,  72),  # 50
    ObjectType(0x03, 0x21,  50, 0,  30),  # 51
    ObjectType(0x03, 0x21,  51, 0,  70),  # 52
    ObjectType(0x03, 0x21,  52, 0,  69),  # 53
    ObjectType(0x03, 0x21,  53, 0,  87),  # 54
    ObjectType(0x03, 0x01,  54, 0,  75),  # 55
    ObjectType(0x03, 0x01,  55, 1,   0),  # 56
    ObjectType(0x03, 0x01,  56, 1,   1),  # 57
    ObjectType(0x03, 0x01,  57, 1,   2),  # 58
    ObjectType(0x03, 0x01,  58, 1,   3),  # 59
    ObjectType(0x03, 0x21,  59, 0,  77),  # 60
    ObjectType(0x03, 0x21,  60, 0,  31),  # 61
    ObjectType(0x03, 0x21,  61, 0,  78),  # 62
    ObjectType(0x03, 0x21,  62, 0,  81),  # 63
    ObjectType(0x03, 0x21,  63, 0,  82),  # 64
    ObjectType(0x03, 0x21,  64, 0,  83),  # 65
    ObjectType(0x03, 0x21,  65, 2,   0),  # 66
    ObjectType(0x03, 0x21,  66, 2,   1),  # 67
    ObjectType(0x03, 0x21,  67, 2,   2),  # 68
    ObjectType(0x03, 0x21,  68, 1,   8),  # 69
    ObjectType(0x03, 0x21, 255, 0,  32),  # 70
    ObjectType(0x03, 0x21, 255, 0,  32),  # 71
    ObjectType(0x00, 0x21, 255, 2,   0),  # 72
    ObjectType(0x00, 0x21, 255, 2,   0),  # 73
    ObjectType(0x00, 0x21, 255, 2,   0),  # 74
    ObjectType(0x03, 0x21, 255, 0,  33),  # 75
    ObjectType(0x03, 0x21, 255, 0,  34),  # 76
    ObjectType(0x03, 0x21, 255, 0,  76),  # 77
    ObjectType(0x03, 0x21,  71, 0,  91),  # 78
    ObjectType(0x00, 0x00, 255, 2,   0),  # 79
    ObjectType(0x03, 0x21, 255, 0,  35),  # 80
    ObjectType(0x03, 0x21, 255, 0,  36),  # 81
    ObjectType(0x00, 0x21, 255, 2,   0),  # 82
    ObjectType(0x03, 0x01, 255, 0,  36),  # 83
    ObjectType(0x03, 0x01, 255, 0,  36),  # 84
    ObjectType(0x00, 0x00, 255, 2,   0),  # 85
    ObjectType(0x01, 0x00, 255, 2,   0),  # 86
    ObjectType(0x01, 0x00, 255, 2,  47),  # 87
    ObjectType(0x01, 0x00, 255, 2,   0),  # 88
    ObjectType(0x00, 0x00, 255, 2,   0),  # 89
    ObjectType(0x03, 0x21,  69, 0,  89),  # 90
    ObjectType(0x03, 0x21,  70, 0,  90),  # 91
    ObjectType(0x03, 0x21,  72, 0,  92),  # 92
    ObjectType(0x00, 0x00, 255, 2,   0),  # 93
    ObjectType(0x00, 0x00, 255, 2,   0),  # 94
    ObjectType(0x00, 0x00, 255, 2,   0),  # 95
    ObjectType(0x00, 0x00, 255, 2,   0),  # 96
    ObjectType(0x00, 0x00, 255, 2,   0),  # 97
    ObjectType(0x03, 0x49, 255, 0,  79),  # 98
    ObjectType(0x03, 0x49, 255, 0,  80),  # 99
    ObjectType(0x03, 0x21, 255, 0,  37),  # 100
    ObjectType(0x03, 0x21, 255, 0,  38),  # 101
    ObjectType(0x03, 0x21, 255, 0,  39),  # 102
    ObjectType(0x03, 0x21, 255, 0,  40),  # 103
    ObjectType(0x03, 0x21, 255, 0,  41),  # 104
    ObjectType(0x03, 0x21, 255, 0,  42),  # 105
    ObjectType(0x00, 0x00, 255, 2,   0),  # 106
    ObjectType(0x00, 0x00, 255, 2,   0),  # 107
    ObjectType(0x00, 0x00, 255, 2,   0),  # 108
    ObjectType(0x00, 0x00, 255, 2,   0),  # 109
    ObjectType(0x03, 0x0a, 255, 0,  43),  # 110
    ObjectType(0x03, 0x0a, 255, 0,  44),  # 111
    ObjectType(0x03, 0x0a, 255, 0,  45),  # 112
    ObjectType(0x03, 0x0a, 255, 0,  46),  # 113
    ObjectType(0x03, 0x02, 255, 0,  47),  # 114
    ObjectType(0x03, 0x02, 255, 0,  48),  # 115
    ObjectType(0x03, 0x02, 255, 0,  49),  # 116
    ObjectType(0x03, 0x0a, 255, 0,  55),  # 117
    ObjectType(0x03, 0x0a, 255, 0,  56),  # 118
    ObjectType(0x03, 0x0a, 255, 0,  57),  # 119
    ObjectType(0x03, 0x0a, 255, 0,  58),  # 120
    ObjectType(0x03, 0x0a, 255, 0,  55),  # 121
    ObjectType(0x03, 0x0a, 255, 0,  56),  # 122
    ObjectType(0x03, 0x0a, 255, 0,  57),  # 123
    ObjectType(0x03, 0x0a, 255, 0,  58),  # 124
    ObjectType(0x03, 0x0a, 255, 0,  59),  # 125
    ObjectType(0x03, 0x0a, 255, 0,  60),  # 126
    ObjectType(0x03, 0x0a, 255, 0,  62),  # 127
    ObjectType(0x03, 0x0a, 255, 0,  63),  # 128
    ObjectType(0x03, 0x0a, 255, 0,  61),  # 129
    ObjectType(0x03, 0x0a, 255, 0,  85),  # 130
    ObjectType(0x03, 0x0a, 255, 0,  64),  # 131
    ObjectType(0x03, 0x0a, 255, 0,  65),  # 132
    ObjectType(0x03, 0x0a, 255, 0,  86),  # 133
    ObjectType(0x03, 0x0a, 255, 0,  66),  # 134
    ObjectType(0x03, 0x0a, 255, 0,  67),  # 135
    ObjectType(0x03, 0x0a, 255, 0,  68),  # 136
    ObjectType(0x03, 0x09, 255, 2,   3),  # 137
    ObjectType(0x03, 0x09, 255, 2,   6),  # 138
    ObjectType(0x03, 0x09, 255, 2,   4),  # 139
    ObjectType(0x03, 0x09, 255, 2,   7),  # 140
    ObjectType(0x03, 0x09, 255, 2,   5),  # 141
    ObjectType(0x03, 0x09, 255, 2,   8),  # 142
    ObjectType(0x03, 0x09, 255, 2,  10),  # 143
    ObjectType(0x03, 0x09, 255, 2,   9),  # 144
    ObjectType(0x03, 0x09, 255, 2,   6),  # 145
    ObjectType(0x03, 0x09, 255, 2,   7),  # 146
    ObjectType(0x03, 0x09, 255, 2,   8),  # 147
    ObjectType(0x03, 0x09, 255, 2,  10),  # 148
    ObjectType(0x03, 0x09, 255, 2,   9),  # 149
    ObjectType(0x03, 0x29, 255, 0,  84),  # 150
    ObjectType(0x03, 0x09, 255, 2,  11),  # 151
    ObjectType(0x03, 0x09, 255, 2,  12),  # 152
    ObjectType(0x03, 0x09, 255, 2,  12),  # 153
    ObjectType(0x03, 0x09, 255, 2,  12),  # 154
    ObjectType(0x03, 0x09, 255, 2,  13),  # 155
    ObjectType(0x03, 0x0a, 255, 0,  86),  # 156
    ObjectType(0x03, 0x0a, 255, 2,  14),  # 157
    ObjectType(0x01, 0x00, 255, 2,   0),  # 158
    ObjectType(0x01, 0x00, 255, 2,   0),  # 159
    ObjectType(0x01, 0x00, 255, 2,   0),  # 160
    ObjectType(0x00, 0x00,   0, 0,   0),  # 161
    ObjectType(0x00, 0x00,   0, 0,   0),  # 162
    ObjectType(0x00, 0x00,   0, 0,   0),  # 163
    ObjectType(0x00, 0x00,   0, 0,   0),  # 164
    ObjectType(0x00, 0x00,   0, 0,   0),  # 165
    ObjectType(0x00, 0x00,   0, 0,   0),  # 166
    ObjectType(0x00, 0x00,   0, 0,   0),  # 167
    ObjectType(0x00, 0x00,   0, 0,   0),  # 168
    ObjectType(0x00, 0x00,   0, 0,   0),  # 169
    ObjectType(0x00, 0x00,   0, 0,   0),  # 170
    ObjectType(0x00, 0x00,   0, 0,   0),  # 171
    ObjectType(0x00, 0x00,   0, 0,   0),  # 172
    ObjectType(0x00, 0x00,   0, 0,   0),  # 173
    ObjectType(0x00, 0x00,   0, 0,   0),  # 174
    ObjectType(0x00, 0x00,   0, 0,   0),  # 175
    ObjectType(0x00, 0x00,   0, 0,   0),  # 176
    ObjectType(0x00, 0x00,   0, 0,   0),  # 177
    ObjectType(0x00, 0x00,   0, 0,   0),  # 178
    ObjectType(0x00, 0x00,   0, 0,   0),  # 179
    ObjectType(0x00, 0x00,   0, 0,   0),  # 180
    ObjectType(0x00, 0x00,   0, 0,   0),  # 181
    ObjectType(0x00, 0x00,   0, 0,   0),  # 182
    ObjectType(0x00, 0x00,   0, 0,   0),  # 183
    ObjectType(0x00, 0x00,   0, 0,   0),  # 184
    ObjectType(0x00, 0x00,   0, 0,   0),  # 185
    ObjectType(0x00, 0x00,   0, 0,   0),  # 186
    ObjectType(0x00, 0x00,   0, 0,   0),  # 187
    ObjectType(0x00, 0x00,   0, 0,   0),  # 188
    ObjectType(0x00, 0x00,   0, 0,   0),  # 189
    ObjectType(0x00, 0x00,   0, 0,   0),  # 190
    ObjectType(0x00, 0x00,   0, 0,   0),  # 191
    ObjectType(0x00, 0x00,   0, 0,   0),  # 192
    ObjectType(0x00, 0x00,   0, 0,   0),  # 193
    ObjectType(0x00, 0x00,   0, 0,   0),  # 194
    ObjectType(0x00, 0x00,   0, 0,   0),  # 195
    ObjectType(0x00, 0x00,   0, 0,   0),  # 196
    ObjectType(0x00, 0x00,   0, 0,   0),  # 197
    ObjectType(0x00, 0x00,   0, 0,   0),  # 198
    ObjectType(0x00, 0x00,   0, 0,   0),  # 199
    ObjectType(0x00, 0x00,   0, 0,   0),  # 200
)
"""The record of each object type 0 to 200, by object type."""

CRAFT_OBJECTS: tuple[int, ...] = (
      0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  # 0-9
      8,   8,  12,  13,  14,  15,  16,  17,  18,  19,  # 10-19
     20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  # 20-29
     30,  26,  32,  33,  34,  35,  36,  37,  38,  38,  # 30-39
     40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  # 40-49
     50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  # 50-59
     60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  # 60-69
     70,  71,  72,  73,  74,  75,  76,  77,  78,  79,  # 70-79
     80,  81,  82,  83,  84,  85, 100,  87, 236,   0,  # 80-89
     90,  91,  92,   0,   0,   0,  # 90-95
)
"""The object type of each craft type 0 to 95, by craft type."""
# fmt: on
