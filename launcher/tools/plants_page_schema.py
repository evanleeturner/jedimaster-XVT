"""The planted faults of the page's message list and its schema builder.

Purpose:
    Break one rule of ``jedimaster/page/protocol.py`` or ``schema.py`` per
    plant, so each example in ``tests/control-examples`` and each schema rule
    fails somewhere.

Flow:
    A plant in ``protocol`` or ``schema`` regenerates the schema file, so it
    fails the example or rule it names, not the drift test; the one drift
    plant does not. ``plants_page`` joins this table with the others.

Invariants:
    - Ids are unique across all tables, all starting ``page-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_page_schema import SCHEMA_FILE_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

PRO = "jedimaster/page/protocol.py"
SCH = "jedimaster/page/schema.py"
TC = "tests/test_page_control.py::"
TX = "tests/test_page_schema.py::"

PROTOCOL_PLANTS: list[Plant] = [
    Plant(
        "page-protocol-status-event-renamed",
        PRO,
        '    event: Literal["status"]',
        '    event: Literal["state"]',
        (
            TX + "test_accepted_example[push-status.json]",
            TC + "test_status_push_is_valid",
        ),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-hello-renamed",
        PRO,
        '    command: Literal["hello"]',
        '    command: Literal["hello2"]',
        (TX + "test_accepted_example[request-hello.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-event-any-text",
        PRO,
        '    event: Literal["status"]',
        "    event: str",
        (TX + "test_refused_example[push-unknown-event.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-scaling-any-text",
        PRO,
        'ArtScaling = Literal["whole_pixels", "engine_fit", "sharp_bilinear"]',
        "ArtScaling = str",
        (TX + "test_refused_example[push-settings-changed-bad-value.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-error-code-any-text",
        PRO,
        'ErrorCode = Literal["bad_message", "unknown_command", "bad_arguments"]',
        "ErrorCode = str",
        (TX + "test_refused_example[reply-error-unknown-code.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-result-may-be-text",
        PRO,
        "        | ShowMissionResult\n    )",
        "        | ShowMissionResult\n        | str\n    )",
        (TX + "test_refused_example[reply-ok-string.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-size-cap",
        PRO,
        "MAX_MESSAGE_BYTES = 64 * 1024",
        "MAX_MESSAGE_BYTES = 32 * 1024",
        (TC + "test_the_size_cap_is_64_kib",),
    ),
    Plant(
        "page-protocol-value-dropped",
        PRO,
        'ArtScaling = Literal["whole_pixels", "engine_fit", "sharp_bilinear"]',
        'ArtScaling = Literal["whole_pixels", "engine_fit"]',
        (TX + "test_accepted_example[request-settings-set.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-revision",
        PRO,
        "    schema_revision: Literal[2]",
        "    schema_revision: Literal[1]",
        (TC + "test_hello", TX + "test_accepted_example[reply-ok-hello.json]"),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-error-code-dropped",
        PRO,
        'ErrorCode = Literal["bad_message", "unknown_command", "bad_arguments"]',
        'ErrorCode = Literal["bad_message", "bad_arguments"]',
        (TX + "test_accepted_example[reply-error-unknown-command.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-id-zero",
        PRO,
        'ID_RANGE = {"minimum": 1, "maximum": MAX_ID}',
        'ID_RANGE = {"minimum": 0, "maximum": MAX_ID}',
        (TX + "test_refused_example[request-id-zero.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-id-over-max",
        PRO,
        'ID_RANGE = {"minimum": 1, "maximum": MAX_ID}',
        'ID_RANGE = {"minimum": 1, "maximum": MAX_ID + 1}',
        (TX + "test_refused_example[request-id-too-large.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-reply-id-negative",
        PRO,
        'REPLY_ID_RANGE = {"minimum": 0, "maximum": MAX_ID}',
        'REPLY_ID_RANGE = {"minimum": 1, "maximum": MAX_ID}',
        (TX + "test_accepted_example[reply-error-bad-message.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-version-length",
        PRO,
        '    page_version: str = field(metadata={"minLength": 1, "maxLength": 64})',
        "    page_version: str",
        (TX + "test_refused_example[request-hello-empty-version.json]",),
        regen_schema=True,
    ),
]

SCHEMA_PLANTS: list[Plant] = [
    Plant(
        "page-schema-examples-without-replies",
        "tests/test_page_schema.py",
        'ACCEPT = sorted((EXAMPLES / "accept").glob("*.json"))',
        'ACCEPT = sorted((EXAMPLES / "accept").glob("request-*.json"))',
        (TX + "test_examples_exist_for_replies_and_pushes",),
    ),
    Plant(
        "page-schema-examples-without-requests",
        "tests/test_page_schema.py",
        'ACCEPT = sorted((EXAMPLES / "accept").glob("*.json"))',
        'ACCEPT = sorted((EXAMPLES / "accept").glob("reply-*.json"))',
        (TX + "test_every_command_has_an_accepted_example",),
    ),
    Plant(
        "page-schema-anything-goes",
        SCH,
        '        "oneOf": [{"$ref": f"#/$defs/{cls.__name__}"} for cls in MESSAGE_CLASSES],',
        '        "anyOf": [{}, *({"$ref": f"#/$defs/{c.__name__}"} for c in MESSAGE_CLASSES)],',
        (
            TX + "test_refused_example[message-array.json]",
            TX + "test_refused_example[message-empty-object.json]",
            TX + "test_refused_example[message-null.json]",
            TX + "test_refused_example[message-text.json]",
        ),
        regen_schema=True,
    ),
    Plant(
        "page-schema-drifts",
        SCH,
        'SCHEMA_ID = "https://jedimaster.invalid/schema/control.schema.json"',
        'SCHEMA_ID = "https://jedimaster.invalid/schema/control2.schema.json"',
        (TX + "test_schema_file_matches_model",),
    ),
    Plant(
        "page-schema-open-objects",
        SCH,
        '    entry["additionalProperties"] = False',
        '    entry["additionalProperties"] = True',
        (
            TX + "test_every_object_is_closed",
            TX + "test_refused_example[request-extra-field.json]",
        ),
        regen_schema=True,
    ),
    Plant(
        "page-schema-nothing-required",
        SCH,
        '        entry["required"] = list(props)',
        '        entry["required"] = []',
        (TX + "test_refused_example[request-args-missing.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-schema-limits-dropped",
        SCH,
        "        prop.update({k: v for k, v in f.metadata.items() if k in LIMIT_KEYS})",
        "        pass",
        (TX + "test_refused_example[request-id-zero.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-schema-enum-widened",
        SCH,
        '    return {"enum": list(values)}',
        '    return {"enum": [*values, "stretched"]}',
        (TX + "test_refused_example[request-set-unknown-value.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-schema-const-loosened",
        SCH,
        '        return {"const": values[0]}',
        '        return {"type": "string"}',
        (TX + "test_accepted_example[request-install-status.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-schema-last-message-dropped",
        SCH,
        '        "oneOf": [{"$ref": f"#/$defs/{cls.__name__}"} for cls in MESSAGE_CLASSES],',
        '        "oneOf": [{"$ref": f"#/$defs/{c.__name__}"} for c in MESSAGE_CLASSES[:-1]],',
        (TX + "test_accepted_example[push-show-mission.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-schema-null-refused",
        SCH,
        '            return {"anyOf": [_type_schema(inner, defs), {"type": "null"}]}',
        '            return {"anyOf": [_type_schema(inner, defs)]}',
        (TX + "test_accepted_example[reply-ok-install-missing.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-schema-array-items-open",
        SCH,
        '        return {"type": "array", "items": _type_schema(inner, defs)}',
        '        return {"type": "array", "items": {}}',
        (TX + "test_refused_example[reply-menu-entry-extra.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-schema-integer-is-anything",
        SCH,
        '        return {"type": "integer"}',
        "        return {}",
        (TX + "test_refused_example[request-id-text.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-schema-integer-is-number",
        SCH,
        '        return {"type": "integer"}',
        '        return {"type": "number"}',
        (TX + "test_refused_example[request-id-fraction.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-schema-boolean-open",
        SCH,
        '        return {"type": "boolean"}',
        "        return {}",
        (TX + "test_refused_example[push-status-wrong-type.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-schema-string-open",
        SCH,
        '        return {"type": "string"}',
        "        return {}",
        (TX + "test_refused_example[request-hello-number-version.json]",),
        regen_schema=True,
    ),
]

SHOW_SCHEMA_PLANTS: list[Plant] = [
    Plant(
        "page-protocol-settings-push-unlisted",
        PRO,
        "    SettingsChangedPush,\n    ShowMissionPush,\n)",
        "    ShowMissionPush,\n)",
        (TX + "test_accepted_example[push-settings-changed.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-show-request-unlisted",
        PRO,
        "    SettingsSetRequest,\n    ShowMissionRequest,\n)\nCOMMANDS",
        "    SettingsSetRequest,\n)\nCOMMANDS",
        (TX + "test_accepted_example[request-show-mission.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-show-push-unlisted",
        PRO,
        "    SettingsChangedPush,\n    ShowMissionPush,\n)",
        "    SettingsChangedPush,\n)",
        (TX + "test_accepted_example[push-show-mission.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-show-result-unlisted",
        PRO,
        "        | ShowMissionResult\n    )",
        "    )",
        (TX + "test_accepted_example[reply-ok-show-mission.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-show-type-campaign-dropped",
        PRO,
        '"training", "melee", "tournament", "combat", "battle", "campaign"\n]',
        '"training", "melee", "tournament", "combat", "battle"\n]',
        (TX + "test_accepted_example[request-show-mission-id-zero.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-show-id-below-zero",
        PRO,
        '    mission_type: MissionTypeName\n    id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})\n\n\n@dataclass\nclass HelloResult',
        '    mission_type: MissionTypeName\n    id: int = field(metadata={"minimum": -1, "maximum": MAX_ID})\n\n\n@dataclass\nclass HelloResult',
        (TX + "test_refused_example[request-show-mission-negative-id.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-show-id-above-max",
        PRO,
        '    mission_type: MissionTypeName\n    id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})\n\n\n@dataclass\nclass HelloResult',
        '    mission_type: MissionTypeName\n    id: int = field(metadata={"minimum": 0, "maximum": MAX_ID + 1})\n\n\n@dataclass\nclass HelloResult',
        (TX + "test_refused_example[request-show-mission-id-too-large.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-show-id-zero-refused",
        PRO,
        '    mission_type: MissionTypeName\n    id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})\n\n\n@dataclass\nclass HelloResult',
        '    mission_type: MissionTypeName\n    id: int = field(metadata={"minimum": 1, "maximum": MAX_ID})\n\n\n@dataclass\nclass HelloResult',
        (TX + "test_accepted_example[request-show-mission-id-zero.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-show-push-id-below-zero",
        PRO,
        '    mission_type: MissionTypeName\n    id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})\n    title: str',
        '    mission_type: MissionTypeName\n    id: int = field(metadata={"minimum": -5, "maximum": MAX_ID})\n    title: str',
        (TX + "test_refused_example[push-show-mission-negative-id.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-show-push-title-dropped",
        PRO,
        '    id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})\n    title: str\n',
        '    id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})\n',
        (TX + "test_refused_example[push-show-mission-no-title.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-show-push-type-free",
        PRO,
        '    mission_type: MissionTypeName\n    id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})\n    title: str',
        '    mission_type: str\n    id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})\n    title: str',
        (TX + "test_refused_example[push-show-mission-unknown-type.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-show-args-type-free",
        PRO,
        '    mission_type: MissionTypeName\n    id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})\n\n\n@dataclass\nclass HelloResult',
        '    mission_type: str\n    id: int = field(metadata={"minimum": 0, "maximum": MAX_ID})\n\n\n@dataclass\nclass HelloResult',
        (TX + "test_refused_example[request-show-mission-unknown-type.json]",),
        regen_schema=True,
    ),
    Plant(
        "page-protocol-shown-is-text",
        PRO,
        "    shown: bool\n",
        "    shown: str\n",
        (TX + "test_refused_example[reply-show-mission-shown-text.json]",),
        regen_schema=True,
    ),
]

SCHEMA_FILE_PLANTS: list[Plant] = [
    *PROTOCOL_PLANTS,
    *SCHEMA_PLANTS,
    *SHOW_SCHEMA_PLANTS,
]
