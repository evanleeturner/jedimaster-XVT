"""The planted faults of the page: server, command line, and the whole table.

Purpose:
    Break one rule of ``jedimaster/page/server.py``, ``cli.py`` or the
    command registration per plant: the guards of the server (host, secret,
    origin, headers, size cap), the socket's pushes and the command line.
    ``PAGE_PLANTS`` joins every page table.

Flow:
    ``plant_faults`` applies each plant alone: write ``new`` over ``old`` in
    ``path``, run the suite, restore, and check that every test in
    ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``page-``.
    - Each ``old`` text occurs exactly once in its file.
    - Every test of the page's Python files fails under at least one plant.

Call:
    ``from plants_page import PAGE_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant
from plants_page_control import CONTROL_FILE_PLANTS
from plants_page_schema import SCHEMA_FILE_PLANTS

logger = logging.getLogger(__name__)

SRV = "jedimaster/page/server.py"
CLI = "jedimaster/page/cli.py"
INIT = "jedimaster/page/__init__.py"
MAIN = "jedimaster/__main__.py"
TV = "tests/test_page_server.py::"
TL = "tests/test_page_cli.py::"

SERVER_PLANTS: list[Plant] = [
    Plant(
        "page-server-host-unchecked",
        SRV,
        '    if request.headers.get("Host", "") not in _allowed_hosts(request):',
        "    if False:",
        (TV + "test_wrong_host_is_refused_even_with_the_key",),
    ),
    Plant(
        "page-server-host-port-ignored",
        SRV,
        '    if request.headers.get("Host", "") not in _allowed_hosts(request):',
        '    if request.headers.get("Host", "").split(":")[0] not in HOSTNAMES:',
        (TV + "test_host_with_the_wrong_port_is_refused",),
    ),
    Plant(
        "page-server-localhost-refused",
        SRV,
        'HOSTNAMES = ("127.0.0.1", "localhost")',
        'HOSTNAMES = ("127.0.0.1",)',
        (TV + "test_both_local_names_are_accepted",),
    ),
    Plant(
        "page-server-no-default-src",
        SRV,
        "f\"default-src 'self'; connect-src 'self'",
        "f\"connect-src 'self'",
        (TV + "test_security_headers_on_every_response",),
    ),
    Plant(
        "page-server-frames-allowed",
        SRV,
        "frame-ancestors 'none'",
        "frame-ancestors *",
        (TV + "test_security_headers_on_every_response",),
    ),
    Plant(
        "page-server-socket-origins-unnamed",
        SRV,
        '    sockets = " ".join(f"ws://{name}:{port}" for name in HOSTNAMES)',
        '    sockets = ""',
        (TV + "test_security_headers_on_every_response",),
    ),
    Plant(
        "page-server-sniffing-allowed",
        SRV,
        '    response.headers["X-Content-Type-Options"] = "nosniff"',
        "    pass",
        (TV + "test_security_headers_on_every_response",),
    ),
    Plant(
        "page-server-referrer-sent",
        SRV,
        '    response.headers["Referrer-Policy"] = "no-referrer"',
        "    pass",
        (TV + "test_security_headers_on_every_response",),
    ),
    Plant(
        "page-server-cache-allowed",
        SRV,
        '    response.headers["Cache-Control"] = "no-store"',
        "    pass",
        (TV + "test_security_headers_on_every_response",),
    ),
    Plant(
        "page-server-errors-without-headers",
        SRV,
        "        _secure(exc, request)\n        raise",
        "        raise",
        (TV + "test_a_request_no_route_answers_still_carries_the_headers",),
    ),
    Plant(
        "page-server-refusals-without-headers",
        SRV,
        "middlewares=[add_headers, check_host, check_key]",
        "middlewares=[check_host, check_key, add_headers]",
        (TV + "test_security_headers_on_every_response",),
    ),
    Plant(
        "page-server-wrong-key-accepted",
        SRV,
        '        if not _same(request.query["key"], secret):',
        "        if False:",
        (TV + "test_wrong_key_is_refused_and_sets_no_cookie",),
    ),
    Plant(
        "page-server-cookie-script-readable",
        SRV,
        '            COOKIE_NAME, secret, httponly=True, samesite="Strict", path="/"',
        '            COOKIE_NAME, secret, httponly=False, samesite="Strict", path="/"',
        (TV + "test_key_sets_a_strict_cookie_and_redirects",),
    ),
    Plant(
        "page-server-cookie-lax",
        SRV,
        '            COOKIE_NAME, secret, httponly=True, samesite="Strict", path="/"',
        '            COOKIE_NAME, secret, httponly=True, samesite="Lax", path="/"',
        (TV + "test_key_sets_a_strict_cookie_and_redirects",),
    ),
    Plant(
        "page-server-cookie-path",
        SRV,
        '            COOKIE_NAME, secret, httponly=True, samesite="Strict", path="/"',
        '            COOKIE_NAME, secret, httponly=True, samesite="Strict", path="/x"',
        (TV + "test_key_sets_a_strict_cookie_and_redirects",),
    ),
    Plant(
        "page-server-redirect-elsewhere",
        SRV,
        'headers={"Location": "/"}',
        'headers={"Location": "/main.js"}',
        (TV + "test_key_sets_a_strict_cookie_and_redirects",),
    ),
    Plant(
        "page-server-no-redirect",
        SRV,
        "web.Response(status=302,",
        "web.Response(status=200,",
        (TV + "test_key_sets_a_strict_cookie_and_redirects",),
    ),
    Plant(
        "page-server-no-cookie-needed",
        SRV,
        '    if not _same(request.cookies.get(COOKIE_NAME, ""), secret):',
        "    if False:",
        (
            TV + "test_no_cookie_is_refused_everywhere",
            TV + "test_wrong_cookie_is_refused",
            TV + "test_socket_needs_the_cookie",
        ),
    ),
    Plant(
        "page-server-secret-compared-plainly",
        SRV,
        '    return hmac.compare_digest(given.encode("utf-8"), wanted.encode("utf-8"))',
        "    return given == wanted",
        (TV + "test_the_secret_is_compared_in_constant_time",),
    ),
    Plant(
        "page-server-key-logged",
        SRV,
        '        logger.info("page opened with the key")',
        '        logger.info("page opened with the key %s", request.query["key"])',
        (TV + "test_the_secret_is_never_logged",),
    ),
    Plant(
        "page-server-guess-logged",
        SRV,
        '    logger.warning("refused %s %s: %s", request.method, request.path, reason)',
        '    logger.warning("refused %s %s: %s", request.method, request.path_qs, reason)',
        (TV + "test_the_secret_is_never_logged",),
    ),
    Plant(
        "page-server-access-log-on",
        SRV,
        'RUNNER_OPTIONS = {"access_log": None}',
        "RUNNER_OPTIONS = {}",
        (TV + "test_serve_binds_locally_prints_the_url_and_logs_no_secret",),
    ),
    Plant(
        "page-server-binds-everywhere",
        SRV,
        "await web.TCPSite(runner, HOST, port).start()",
        'await web.TCPSite(runner, "0.0.0.0", port).start()',
        (TV + "test_serve_binds_locally_prints_the_url_and_logs_no_secret",),
    ),
    Plant(
        "page-server-url-not-printed",
        SRV,
        "        print(url, flush=True)",
        "        pass",
        (TV + "test_serve_binds_locally_prints_the_url_and_logs_no_secret",),
    ),
    Plant(
        "page-server-opens-when-told-not-to",
        SRV,
        "        if open_browser:",
        "        if not open_browser:",
        (TV + "test_serve_binds_locally_prints_the_url_and_logs_no_secret",),
    ),
    Plant(
        "page-server-page-never-found",
        SRV,
        "    if not page.is_file():",
        "    if True:",
        (TV + "test_page_is_served_after_the_redirect",),
    ),
    Plant(
        "page-server-missing-page-crashes",
        SRV,
        "    if not page.is_file():",
        "    if False:",
        (TV + "test_a_page_that_is_not_built_says_so",),
    ),
    Plant(
        "page-server-no-static-files",
        SRV,
        '        app.router.add_static("/", page_dir)',
        "        pass",
        (TV + "test_a_script_is_served",),
    ),
    Plant(
        "page-server-serves-the-parent",
        SRV,
        '        app.router.add_static("/", page_dir)',
        '        app.router.add_static("/", page_dir.parent)',
        (TV + "test_only_the_page_folder_is_served",),
    ),
    Plant(
        "page-server-static-needs-folder",
        SRV,
        "    if page_dir.is_dir():",
        "    if True:",
        (TV + "test_a_page_that_is_not_built_says_so",),
    ),
    Plant(
        "page-server-origin-unchecked",
        SRV,
        "    if origin != f\"http://{request.headers.get('Host', '')}\":",
        "    if False:",
        (
            TV + "test_wrong_origin_is_refused_before_the_upgrade",
            TV + "test_origin_with_another_port_is_refused",
        ),
    ),
    Plant(
        "page-server-no-size-cap",
        SRV,
        "web.WebSocketResponse(max_msg_size=MAX_MESSAGE_BYTES)",
        "web.WebSocketResponse(max_msg_size=0)",
        (TV + "test_oversize_message_closes_the_socket",),
    ),
    Plant(
        "page-server-cap-too-small",
        SRV,
        "web.WebSocketResponse(max_msg_size=MAX_MESSAGE_BYTES)",
        "web.WebSocketResponse(max_msg_size=1024)",
        (TV + "test_message_at_the_cap_is_answered",),
    ),
    Plant(
        "page-server-no-status-push",
        SRV,
        "        await _send(ws, control.status_push())",
        "        pass",
        (
            TV + "test_status_arrives_on_connect",
            TV + "test_every_command_over_the_socket",
        ),
    ),
    Plant(
        "page-server-binary-ends-the-socket",
        SRV,
        "            elif msg.type == WSMsgType.BINARY:",
        "            elif False:",
        (TV + "test_refusals_over_the_socket_keep_it_open",),
    ),
    Plant(
        "page-server-stop-not-logged",
        SRV,
        '                logger.warning("socket stopped: %s", msg.type.name)',
        '                logger.debug("socket stopped: %s", msg.type.name)',
        (TV + "test_oversize_message_closes_the_socket",),
    ),
    Plant(
        "page-server-pushes-to-nobody",
        SRV,
        "        for other in list(app[SOCKETS]):",
        "        for other in []:",
        (TV + "test_a_setting_reaches_a_second_page_and_the_file",),
    ),
    Plant(
        "page-server-push-before-reply",
        SRV,
        "            await _send(ws, outcome.reply)\n            await _broadcast(request.app, outcome)",
        "            await _broadcast(request.app, outcome)\n            await _send(ws, outcome.reply)",
        (TV + "test_a_setting_reaches_a_second_page_and_the_file",),
    ),
    Plant(
        "page-server-closed-page-kept",
        SRV,
        "        request.app[SOCKETS].discard(ws)",
        "        pass",
        (TV + "test_a_closed_page_is_dropped_from_the_pushes",),
    ),
    Plant(
        "page-server-connect-not-logged",
        SRV,
        '    logger.info("page connected")',
        '    logger.debug("page connected")',
        (TV + "test_connect_and_close_are_logged",),
    ),
    Plant(
        "page-server-message-ignored",
        SRV,
        "                outcome = control.handle(msg.data)",
        '                outcome = control.handle("{}")',
        (TV + "test_every_command_over_the_socket",),
    ),
]

CLI_PLANTS: list[Plant] = [
    Plant(
        "page-cli-default-port",
        CLI,
        "DEFAULT_PORT = 8780",
        "DEFAULT_PORT = 8781",
        (
            TL + "test_defaults_and_options",
            TL + "test_the_browser_opens_unless_told_not_to",
        ),
    ),
    Plant(
        "page-cli-default-page-folder",
        CLI,
        'DEFAULT_PAGE_DIR = Path(__file__).resolve().parents[2] / "page" / "dist"',
        'DEFAULT_PAGE_DIR = Path(__file__).resolve().parents[2] / "pages" / "dist"',
        (TL + "test_defaults_and_options",),
    ),
    Plant(
        "page-cli-missing-folder-served",
        CLI,
        "    if not page_dir.is_dir():",
        "    if False:",
        (TL + "test_a_missing_page_folder_exits_2",),
    ),
    Plant(
        "page-cli-any-port",
        CLI,
        "    if not 0 <= args.port <= 65535:",
        "    if False:",
        (TL + "test_a_port_out_of_range_exits_2",),
    ),
    Plant(
        "page-cli-fixed-secret",
        CLI,
        "    key = secrets.token_urlsafe(32)",
        '    key = "fixed-secret-fixed-secret-fixed-secret-fixed-secret"',
        (TL + "test_each_run_draws_a_new_secret",),
    ),
    Plant(
        "page-cli-short-secret",
        CLI,
        "    key = secrets.token_urlsafe(32)",
        "    key = secrets.token_urlsafe(8)",
        (TL + "test_run_passes_the_options_to_serve",),
    ),
    Plant(
        "page-cli-bind-failure-ignored",
        CLI,
        '        logger.error("cannot serve on port %d: %s", args.port, exc)\n        return 2',
        '        logger.error("cannot serve on port %d: %s", args.port, exc)\n        return 0',
        (TL + "test_a_port_that_cannot_be_bound_exits_2",),
    ),
    Plant(
        "page-cli-ctrl-c-fails",
        CLI,
        '        logger.info("stopped")',
        "        return 3",
        (TL + "test_ctrl_c_exits_0",),
    ),
    Plant(
        "page-cli-install-argument-ignored",
        CLI,
        "    install = find_install(args.install) if args.install else find_install()",
        "    install = find_install()",
        (TL + "test_run_passes_the_options_to_serve",),
    ),
    Plant(
        "page-cli-settings-argument-ignored",
        CLI,
        "    settings_path = Path(args.settings) if args.settings else default_settings_path()",
        "    settings_path = default_settings_path()",
        (TL + "test_run_passes_the_options_to_serve",),
    ),
    Plant(
        "page-cli-no-open-inverted",
        CLI,
        "args.port, page_dir, not args.no_open",
        "args.port, page_dir, args.no_open",
        (TL + "test_run_passes_the_options_to_serve",),
    ),
    Plant(
        "page-cli-missing-framework-crashes",
        CLI,
        "    except ImportError:",
        "    except KeyError:",
        (TL + "test_without_the_web_framework_the_command_says_so",),
    ),
    Plant(
        "page-cli-no-install-stops",
        CLI,
        '        logger.warning("no install found: the page will say so")',
        "        return 2",
        (TL + "test_no_install_still_serves",),
    ),
    Plant(
        "page-main-command-unlisted",
        MAIN,
        '    "page": page_cli,\n',
        "",
        (TL + "test_run_passes_the_options_to_serve",),
    ),
    Plant(
        "page-main-parser-unlisted",
        MAIN,
        "    page_cli.add_parser(sub)\n",
        "",
        (TL + "test_run_passes_the_options_to_serve",),
    ),
    Plant(
        "page-framework-imported-early",
        INIT,
        "import logging\n",
        "import logging\n\nimport aiohttp  # noqa: F401\n",
        (TL + "test_other_commands_do_not_import_the_web_framework",),
    ),
]

PAGE_PLANTS: list[Plant] = [
    *CONTROL_FILE_PLANTS,
    *SCHEMA_FILE_PLANTS,
    *SERVER_PLANTS,
    *CLI_PLANTS,
]
