"""The planted faults of the menu frame pictures: the screen, the store, the route.

Purpose:
    Break one rule of ``jedimaster/briefing/frame.py`` (the set of twelve
    names, the background file, the cache), of the whole-screen composition
    in ``under.py`` that the frame and the under-map picture share, and of
    the ``/art/frame/<name>`` route in ``page/server.py``.

Flow:
    ``plant_faults`` applies each plant alone: write ``new`` over ``old``
    in ``path``, run the suite, restore, and check that every test in
    ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``briefing-frame-``.
    - Each ``old`` text occurs exactly once in its file.
    - Every test of ``tests/test_briefing_frame.py`` fails under at least
      one plant.

Call:
    ``from plants_briefing_frame import BRIEFING_FRAME_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

F = "jedimaster/briefing/frame.py"
U = "jedimaster/briefing/under.py"
S = "jedimaster/page/server.py"
TF = "tests/test_briefing_frame.py::"
TU = "tests/test_briefing_under.py::"
LAYERS = "(background, True), (pad, False), (frame, False)"
KEEPS = TF + "test_the_background_keeps_index_0_and_the_pad_and_frame_skip_it"
CROP = (
    TU + "test_the_panel_is_the_part_of_the_picture_at_84_96",
    TU + "test_a_smaller_layer_covers_only_its_own_part_of_the_panel",
)


def plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    """Return the plant ``briefing-frame-<pid>``."""
    return Plant(f"briefing-frame-{pid}", path, old, new, expect)


NAMES_PLANTS: list[Plant] = [
    plant(
        "background-file-name",
        F,
        'read_layer(install, f"frontres\\\\{background}.bmp")',
        'read_layer(install, f"frontres\\\\{background}1.bmp")',
        TF + "test_every_name_builds_a_640_by_480_picture",
    ),
    plant(
        "name-missing-from-the-set",
        F,
        '    "gamecai",\n    "gametrn",\n)',
        '    "gamecai",\n)',
        TF + "test_the_set_is_twelve_frame_names",
    ),
    plant(
        "name-doubled-in-the-set",
        F,
        '    "gamecai",\n    "gametrn",\n)',
        '    "gamecai",\n    "gamecai",\n)',
        TF + "test_the_set_is_twelve_frame_names",
    ),
    plant(
        "art-name-prefix",
        F,
        'FRAME_PREFIX = "frame-"',
        'FRAME_PREFIX = "screen-"',
        TF + "test_the_set_is_twelve_frame_names",
    ),
    plant(
        "pad-file-swapped-for-the-frame",
        F,
        "    pad = read_layer(install, PAD_FILE)\n",
        "    pad = read_layer(install, FRAME_FILE)\n",
        TF + "test_the_pad_and_the_frame_are_the_installs_own_files",
    ),
    plant(
        "frame-file-swapped-for-the-pad",
        F,
        "    frame = read_layer(install, FRAME_FILE)\n",
        "    frame = read_layer(install, PAD_FILE)\n",
        TF + "test_the_pad_and_the_frame_are_the_installs_own_files",
    ),
    plant(
        "pad-left-out-of-the-picture",
        F,
        "    pad = read_layer(install, PAD_FILE)\n",
        "    pad = None\n",
        TF + "test_the_pad_and_the_frame_are_the_installs_own_files",
    ),
    plant(
        "frame-left-out-of-the-picture",
        F,
        "    frame = read_layer(install, FRAME_FILE)\n",
        "    frame = None\n",
        TF + "test_the_pad_and_the_frame_are_the_installs_own_files",
    ),
    plant(
        "picture-size-swapped",
        F,
        "SCREEN_WIDTH, SCREEN_HEIGHT)",
        "SCREEN_HEIGHT, SCREEN_WIDTH)",
        TF + "test_every_name_builds_a_640_by_480_picture",
    ),
    plant(
        "no-background-is-a-blank-picture",
        F,
        '        logger.debug("frame picture %s: no background", background)\n        return None\n',
        '        logger.debug("frame picture %s: no background", background)\n',
        TF + "test_a_failed_build_is_none_with_a_warning_and_tried_again",
    ),
]

STORE_PLANTS: list[Plant] = [
    plant(
        "failed-build-cached",
        F,
        '            logger.warning("cannot build %s: %s", name, exc)\n            return None\n',
        '            logger.warning("cannot build %s: %s", name, exc)\n            self._kept[name] = None  # type: ignore[assignment]\n            return None\n',
        TF + "test_a_build_that_raises_is_none_with_a_warning_and_tried_again",
    ),
    plant(
        "missing-background-cached",
        F,
        "        if png is None:\n            return None\n",
        "        if png is None:\n            self._kept[name] = None  # type: ignore[assignment]\n            return None\n",
        TF + "test_a_failed_build_is_none_with_a_warning_and_tried_again",
    ),
    plant(
        "built-picture-not-kept",
        F,
        "        self._kept[name] = art\n",
        "",
        TF + "test_a_built_picture_is_kept",
    ),
    plant(
        "name-outside-the-set-tried",
        F,
        "        if name not in FRAME_ART or self.install is None:\n",
        "        if not name.startswith(FRAME_PREFIX) or self.install is None:\n",
        TF + "test_any_other_name_is_nothing",
    ),
    plant(
        "no-install-still-served",
        F,
        "        if name not in FRAME_ART or self.install is None:\n",
        "        if name not in FRAME_ART:\n",
        TF + "test_no_install_serves_nothing",
    ),
    plant(
        "picture-type-lost",
        F,
        "        art = Art(png, PNG_TYPE)\n",
        '        art = Art(png, "application/octet-stream")\n',
        TF + "test_every_name_builds_a_640_by_480_picture",
    ),
]

SCREEN_PLANTS: list[Plant] = [
    plant(
        "screen-width-off",
        U,
        "SCREEN_WIDTH = 640\n",
        "SCREEN_WIDTH = 641\n",
        TF + "test_every_name_builds_a_640_by_480_picture",
    ),
    plant(
        "background-skips-index-0",
        U,
        LAYERS,
        "(background, False), (pad, False), (frame, False)",
        KEEPS,
        TU + "test_the_background_keeps_every_pixel_index_0_included",
    ),
    plant(
        "pad-keeps-index-0",
        U,
        LAYERS,
        "(background, True), (pad, True), (frame, False)",
        KEEPS,
        TU + "test_the_pad_and_the_frame_skip_index_0_and_the_shading_skips_it_too",
    ),
    plant(
        "frame-keeps-index-0",
        U,
        LAYERS,
        "(background, True), (pad, False), (frame, True)",
        KEEPS,
        TU + "test_the_pad_and_the_frame_skip_index_0_and_the_shading_skips_it_too",
    ),
    plant(
        "pad-left-out-of-the-screen",
        U,
        LAYERS,
        "(background, True), (frame, False)",
        KEEPS,
    ),
    plant(
        "frame-left-out-of-the-screen",
        U,
        LAYERS,
        "(background, True), (pad, False)",
        TU + "test_the_layers_go_in_order_background_pad_frame_shading",
    ),
    plant(
        "background-left-out-of-the-screen",
        U,
        LAYERS,
        "(pad, False), (frame, False)",
        TU + "test_a_solid_first_layer_shows_everywhere_outside_the_band",
    ),
    plant(
        "frame-before-pad-on-the-screen",
        U,
        LAYERS,
        "(background, True), (frame, False), (pad, False)",
        TF + "test_the_frame_goes_over_the_pad",
    ),
    plant(
        "wide-layer-not-clipped",
        U,
        "        columns = min(SCREEN_WIDTH, layer.width)\n",
        "        columns = layer.width\n",
        TF + "test_a_layer_bigger_than_the_screen_is_clipped",
    ),
    plant(
        "tall-layer-not-clipped",
        U,
        "        for row in range(min(SCREEN_HEIGHT, layer.height)):\n",
        "        for row in range(layer.height):\n",
        TF + "test_a_layer_bigger_than_the_screen_is_clipped",
    ),
    plant(
        "crop-one-row-down",
        U,
        "        start = (PANEL_TOP + row) * SCREEN_WIDTH + PANEL_LEFT\n",
        "        start = (PANEL_TOP + row + 1) * SCREEN_WIDTH + PANEL_LEFT\n",
        *CROP,
    ),
    plant(
        "crop-one-column-right",
        U,
        "        start = (PANEL_TOP + row) * SCREEN_WIDTH + PANEL_LEFT\n",
        "        start = (PANEL_TOP + row) * SCREEN_WIDTH + PANEL_LEFT + 1\n",
        *CROP,
    ),
    plant(
        "crop-one-column-narrow",
        U,
        "        panel += screen[start : start + PANEL_WIDTH]\n",
        "        panel += screen[start : start + PANEL_WIDTH - 1]\n",
        TU + "test_the_background_keeps_every_pixel_index_0_included",
    ),
]

ROUTE_PLANTS: list[Plant] = [
    plant(
        "route-uses-the-briefing-store",
        S,
        'found = request.app[FRAMES].get(request.match_info["name"])',
        'found = request.app[ART].get(request.match_info["name"])',
        TF + "test_each_frame_name_is_served",
    ),
    plant(
        "route-serves-one-picture",
        S,
        'found = request.app[FRAMES].get(request.match_info["name"])',
        'found = request.app[FRAMES].get("frame-gametr.png")',
        TF + "test_each_frame_name_is_served",
    ),
    plant(
        "route-store-has-no-install",
        S,
        "app[FRAMES] = FrameStore(control.install)",
        "app[FRAMES] = FrameStore(None)",
        TF + "test_each_frame_name_is_served",
    ),
    plant(
        "route-type-lost",
        S,
        '    return _refuse(request, 404, "no such frame")\n    return web.Response(body=found.data, content_type=found.content_type)',
        '    return _refuse(request, 404, "no such frame")\n    return web.Response(body=found.data, content_type="application/octet-stream")',
        TF + "test_each_frame_name_is_served",
    ),
    plant(
        "route-refusal-is-ok",
        S,
        '_refuse(request, 404, "no such frame")',
        '_refuse(request, 200, "no such frame")',
        TF + "test_any_other_name_is_404",
    ),
    plant(
        "route-not-added",
        S,
        '    app.router.add_get("/art/frame/{name}", frame)\n',
        "",
        TF + "test_each_frame_name_is_served",
    ),
]

BRIEFING_FRAME_PLANTS: list[Plant] = [
    *NAMES_PLANTS,
    *STORE_PLANTS,
    *SCREEN_PLANTS,
    *ROUTE_PLANTS,
]
