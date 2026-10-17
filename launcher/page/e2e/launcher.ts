import type { ChildProcess } from "node:child_process";
import { spawn, spawnSync } from "node:child_process";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";

import type { BrowserContext, Page } from "@playwright/test";
import { expect as baseExpect, test as base } from "@playwright/test";

const LAUNCHER_FOLDER = join(import.meta.dirname, "..", "..");
const PAGE_FOLDER = join(import.meta.dirname, "..", "dist");
const URL_LINE = /^http:\/\/127\.0\.0\.1:\d+\/\?key=\S+$/;
const START_TIMEOUT_MS = 15_000;

const TRAIN_MENU = [
  "[Basic]",
  "1",
  "ALPHA.TIE",
  "First Flight",
  "2",
  "& BRAVO.TIE",
  "Second Flight",
  "",
].join("\r\n");
// Ten filler missions come first so that "Open Field" sits below the fold of
// its scrolling list: only a scroll to the mark brings it into view.
const MELEE_FILLER = Array.from({ length: 10 }, (_, i) => [
  String(101 + i),
  `FILL${String(i)}.TIE`,
  `Filler ${String(i)}`,
]).flat();
const MELEE_MENU = [
  "[Open]",
  ...MELEE_FILLER,
  "7",
  "DELTA.TIE",
  "Open Field",
  "[Later]",
  "8",
  "DELTA.TIE",
  "Later Field",
  "",
].join("\r\n");

export interface Launcher {
  readonly url: string;
  readonly origin: string;
  readonly key: string;
  readonly installFolder: string;
  readonly settingsFile: string;
  readonly stop: () => Promise<void>;
  /** Sets what the stand-in game program does next: "hang", "done", or "done4" (a version 4 result). */
  readonly behave: (word: "hang" | "done" | "done4") => Promise<void>;
  readonly readSettings: () => Promise<unknown>;
}

const TESTS_FOLDER = join(LAUNCHER_FOLDER, "tests");

// Adds made-up art and one made-up mission (Train/ALPHA.TIE) to the install.
function addBriefing(install: string): void {
  const python = process.env.JEDIMASTER_PYTHON ?? "python3";
  const done = spawnSync(
    python,
    [join(TESTS_FOLDER, "write_briefing_install.py"), install],
    {
      cwd: LAUNCHER_FOLDER,
      encoding: "utf8",
      env: { ...process.env, PYTHONPATH: TESTS_FOLDER },
    },
  );
  if (done.status !== 0) {
    throw new Error(`the made-up briefing was not written: ${done.stderr}`);
  }
}

// Adds twelve made-up menu frame backgrounds (frontres/GAME*.BMP) to the install.
function addFrames(install: string): void {
  const python = process.env.JEDIMASTER_PYTHON ?? "python3";
  const done = spawnSync(
    python,
    [join(TESTS_FOLDER, "write_frame_install.py"), install],
    {
      cwd: LAUNCHER_FOLDER,
      encoding: "utf8",
      env: { ...process.env, PYTHONPATH: TESTS_FOLDER },
    },
  );
  if (done.status !== 0) {
    throw new Error(`the made-up frames were not written: ${done.stderr}`);
  }
}

// Adds solo lists, a pilot folder and the stand-in game program under root.
function addBattle(install: string, root: string): void {
  const python = process.env.JEDIMASTER_PYTHON ?? "python3";
  const done = spawnSync(
    python,
    [join(TESTS_FOLDER, "write_battle_install.py"), install, root],
    {
      cwd: LAUNCHER_FOLDER,
      encoding: "utf8",
      env: { ...process.env, PYTHONPATH: TESTS_FOLDER },
    },
  );
  if (done.status !== 0) {
    throw new Error(
      `the made-up battle files were not written: ${done.stderr}`,
    );
  }
}

async function makeInstall(root: string, frames: boolean): Promise<string> {
  const install = join(root, "XvT");
  await mkdir(join(install, "Train"), { recursive: true });
  await mkdir(join(install, "Melee"), { recursive: true });
  await mkdir(join(install, "BalanceOfPower", "TRAIN"), { recursive: true });
  await writeFile(join(install, "Train", "MISSION.LST"), TRAIN_MENU, "latin1");
  await writeFile(join(install, "Melee", "MISSION.LST"), MELEE_MENU, "latin1");
  addBriefing(install);
  addBattle(install, root);
  if (frames) addFrames(install);
  return install;
}

function waitForUrl(child: ChildProcess): Promise<string> {
  return new Promise((resolve, reject) => {
    let seen = "";
    const timer = setTimeout(() => {
      reject(new Error(`the launcher printed no URL; saw: ${seen}`));
    }, START_TIMEOUT_MS);
    child.stdout?.setEncoding("utf8");
    child.stdout?.on("data", (chunk: string) => {
      seen += chunk;
      const line = seen.split("\n").find((l) => URL_LINE.test(l.trim()));
      if (line !== undefined) {
        clearTimeout(timer);
        resolve(line.trim());
      }
    });
    child.on("exit", (code) => {
      clearTimeout(timer);
      reject(
        new Error(`the launcher exited with ${String(code)}; saw: ${seen}`),
      );
    });
  });
}

async function startLauncher(root: string, frames: boolean): Promise<Launcher> {
  const installFolder = await makeInstall(root, frames);
  const settingsFile = join(root, "config", "settings.json");
  const python = process.env.JEDIMASTER_PYTHON ?? "python3";
  const child = spawn(
    python,
    ["-m", "jedimaster", "page", "--install", installFolder, "--port", "0"]
      .concat(["--no-open", "--page-dir", PAGE_FOLDER])
      .concat(["--settings", settingsFile])
      .concat(["--engine", join(root, "engine.py")])
      .concat(["--pilot-folder", join(root, "pilots")]),
    { cwd: LAUNCHER_FOLDER, stdio: ["ignore", "pipe", "pipe"] },
  );
  const url = await waitForUrl(child);
  const parsed = new URL(url);
  return {
    url,
    origin: parsed.origin,
    key: parsed.searchParams.get("key") ?? "",
    installFolder,
    settingsFile,
    stop: () =>
      new Promise((resolve) => {
        if (child.exitCode !== null || child.signalCode !== null) {
          resolve();
          return;
        }
        child.once("exit", () => {
          resolve();
        });
        child.kill("SIGTERM");
      }),
    behave: (word) => writeFile(join(root, "behave"), word, "utf8"),
    readSettings: async () =>
      JSON.parse(await readFile(settingsFile, "utf8")) as unknown,
  };
}

function launcherFixture(frames: boolean) {
  return async (
    // Playwright reads the fixture's needs from this pattern; it needs none.
    // eslint-disable-next-line no-empty-pattern
    {}: object,
    use: (launcher: Launcher) => Promise<void>,
  ): Promise<void> => {
    const root = await mkdtemp(join(tmpdir(), "jedimaster-page-"));
    const launcher = await startLauncher(root, frames);
    try {
      await use(launcher);
    } finally {
      await launcher.stop();
      await rm(root, { recursive: true, force: true });
    }
  };
}

/** The plain fixture: an install without the menu frame backgrounds. */
export const test = base.extend<{ launcher: Launcher }>({
  launcher: launcherFixture(false),
});

/** The framed fixture: the same install plus made-up frame backgrounds. */
export const framedTest = base.extend<{ launcher: Launcher }>({
  launcher: launcherFixture(true),
});

export const expect = baseExpect;

/** Open the menu whose button reads `name` (Missions, Battle, Pilot, ...). */
export async function openMenu(page: Page, name: string): Promise<void> {
  const button = page.locator(".menu-btn", { hasText: name });
  if ((await button.getAttribute("aria-expanded")) !== "true") {
    await button.click();
  }
  await expect(button).toHaveAttribute("aria-expanded", "true");
}

/** Show `title` from the Missions menu, picking its kind first. */
export async function showMission(
  page: Page,
  title: string,
  kind = "Training",
): Promise<void> {
  await openMenu(page, "Missions");
  await page
    .locator("#missions-kinds")
    .getByRole("button", { name: new RegExp(`^${kind}`) })
    .click();
  await page
    .getByRole("button", { name: `Show ${title}`, exact: true })
    .click();
}

/**
 * Call `command` from a second socket and return its reply; the page under
 * test sees whatever pushes the call causes.
 */
export async function callFromSecondSocket(
  context: BrowserContext,
  origin: string,
  command: string,
  args: Record<string, unknown>,
): Promise<{ ok: boolean; result: Record<string, unknown> }> {
  const caller = await context.newPage();
  await caller.goto(`${origin}/style.css`);
  const reply = await caller.evaluate(
    (call) =>
      new Promise<unknown>((resolve, reject) => {
        const socket = new WebSocket(`ws://${location.host}/ws`);
        socket.addEventListener("error", () => {
          reject(new Error("the second socket failed"));
        });
        socket.addEventListener("message", (event: MessageEvent<string>) => {
          const message = JSON.parse(event.data) as Record<string, unknown>;
          if ("ok" in message) {
            socket.close();
            resolve(message);
          }
        });
        socket.addEventListener("open", () => {
          socket.send(
            JSON.stringify({ id: 1, command: call.command, args: call.args }),
          );
        });
      }),
    { command, args },
  );
  await caller.close();
  return reply as { ok: boolean; result: Record<string, unknown> };
}

/**
 * Make the page's socket name `pilot` in its status push (the page server
 * has no bot to know a pilot) and return a function that sends the page any
 * further server push. Call before `page.goto`.
 */
export async function routePilot(
  page: Page,
  pilot: string | null,
): Promise<(message: object) => void> {
  let toPage: ((text: string) => void) | null = null;
  await page.routeWebSocket(/\/ws$/, (ws) => {
    const server = ws.connectToServer();
    server.onMessage((message) => {
      if (typeof message === "string") {
        const parsed = JSON.parse(message) as {
          event?: string;
          data?: Record<string, unknown>;
        };
        if (parsed.event === "status" && parsed.data !== undefined) {
          parsed.data.pilot = pilot;
          ws.send(JSON.stringify(parsed));
          return;
        }
      }
      ws.send(message);
    });
    ws.onMessage((message) => {
      server.send(message);
    });
    toPage = (text) => {
      ws.send(text);
    };
  });
  return (message) => {
    if (toPage === null) throw new Error("the page has no socket yet");
    toPage(JSON.stringify(message));
  };
}
