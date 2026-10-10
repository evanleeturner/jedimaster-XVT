import type { ChildProcess } from "node:child_process";
import { spawn } from "node:child_process";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";

import { test as base } from "@playwright/test";

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
const MELEE_MENU = ["[Open]", "7", "DELTA.TIE", "Open Field", ""].join("\r\n");

export interface Launcher {
  readonly url: string;
  readonly origin: string;
  readonly key: string;
  readonly installFolder: string;
  readonly settingsFile: string;
  readonly stop: () => Promise<void>;
  readonly readSettings: () => Promise<unknown>;
}

async function makeInstall(root: string): Promise<string> {
  const install = join(root, "XvT");
  await mkdir(join(install, "Train"), { recursive: true });
  await mkdir(join(install, "Melee"), { recursive: true });
  await mkdir(join(install, "BalanceOfPower", "TRAIN"), { recursive: true });
  await writeFile(join(install, "Train", "MISSION.LST"), TRAIN_MENU, "latin1");
  await writeFile(join(install, "Melee", "MISSION.LST"), MELEE_MENU, "latin1");
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

async function startLauncher(root: string): Promise<Launcher> {
  const installFolder = await makeInstall(root);
  const settingsFile = join(root, "config", "settings.json");
  const python = process.env.JEDIMASTER_PYTHON ?? "python3";
  const child = spawn(
    python,
    ["-m", "jedimaster", "page", "--install", installFolder, "--port", "0"]
      .concat(["--no-open", "--page-dir", PAGE_FOLDER])
      .concat(["--settings", settingsFile]),
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
    readSettings: async () =>
      JSON.parse(await readFile(settingsFile, "utf8")) as unknown,
  };
}

export const test = base.extend<{ launcher: Launcher }>({
  // Playwright reads the fixture's needs from this pattern; it needs none.
  // eslint-disable-next-line no-empty-pattern
  launcher: async ({}, use) => {
    const root = await mkdtemp(join(tmpdir(), "jedimaster-page-"));
    const launcher = await startLauncher(root);
    try {
      await use(launcher);
    } finally {
      await launcher.stop();
      await rm(root, { recursive: true, force: true });
    }
  },
});

export { expect } from "@playwright/test";
