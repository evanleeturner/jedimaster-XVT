// Paint lists and frames in a real browser and write their packed pixels.
//
// Usage: node scripts/pixels.ts REQUEST.json OUT.json
// REQUEST.json: { "python": "<python>", "install": "<install folder>",
//   "bundles": [<briefing bundle>...], "jobs": [<job of src/briefing/pixels.ts>...] }
// OUT.json: { "<job name>": "<base64 of 360 x 240 pixels, 5-6-5, low byte first>" }
//
// It starts the launcher on the built page and the install, opens the page
// in Chromium (which sets the run's key), and runs `paintJobs` there.

import { spawn } from "node:child_process";
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

import { chromium } from "@playwright/test";

interface Request {
  readonly python: string;
  readonly install: string;
  readonly bundles: unknown[];
  readonly jobs: unknown[];
}

const LAUNCHER_FOLDER = join(import.meta.dirname, "..", "..");
const PAGE_FOLDER = join(import.meta.dirname, "..", "dist");
const URL_LINE = /^http:\/\/127\.0\.0\.1:\d+\/\?key=\S+$/;
const PIXELS_MODULE = "/briefing/pixels.js";
const START_TIMEOUT_MS = 20_000;

function startLauncher(request: Request): Promise<{
  readonly url: string;
  readonly stop: () => void;
}> {
  const child = spawn(
    request.python,
    [
      "-m",
      "jedimaster",
      "page",
      "--install",
      request.install,
      "--port",
      "0",
    ].concat(["--no-open", "--page-dir", PAGE_FOLDER]),
    { cwd: LAUNCHER_FOLDER, stdio: ["ignore", "pipe", "pipe"] },
  );
  return new Promise((resolve, reject) => {
    let seen = "";
    const timer = setTimeout(() => {
      child.kill("SIGTERM");
      reject(new Error(`the launcher printed no URL; saw: ${seen}`));
    }, START_TIMEOUT_MS);
    child.stdout.setEncoding("utf8");
    child.stdout.on("data", (chunk: string) => {
      seen += chunk;
      const line = seen.split("\n").find((l) => URL_LINE.test(l.trim()));
      if (line !== undefined) {
        clearTimeout(timer);
        resolve({
          url: line.trim(),
          stop: () => {
            child.kill("SIGTERM");
          },
        });
      }
    });
    child.on("exit", (code) => {
      clearTimeout(timer);
      reject(new Error(`the launcher exited with ${String(code)}`));
    });
  });
}

const [requestPath, outPath] = process.argv.slice(2);
if (requestPath === undefined || outPath === undefined) {
  throw new Error("usage: node scripts/pixels.ts REQUEST.json OUT.json");
}
const request = JSON.parse(readFileSync(requestPath, "utf8")) as Request;
const launcher = await startLauncher(request);
const browser = await chromium.launch();
try {
  const page = await browser.newPage();
  await page.goto(launcher.url);
  const painted = await page.evaluate(
    async ({ bundles, jobs, path }) => {
      const module = (await import(path)) as {
        paintJobs: (b: unknown[], j: unknown[]) => Promise<unknown>;
      };
      return module.paintJobs(bundles, jobs);
    },
    { bundles: request.bundles, jobs: request.jobs, path: PIXELS_MODULE },
  );
  writeFileSync(outPath, JSON.stringify(painted));
} finally {
  await browser.close();
  launcher.stop();
}
