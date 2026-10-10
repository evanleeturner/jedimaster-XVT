// Run briefing recipes on the built player and print their lines as JSON.
//
// Usage: node scripts/briefing-sheet.ts JOBS.json
// JOBS.json: { "bundle": "<path of a bundle>", "jobs": [{ "name", "team", "steps" }] }
// Prints: { "<name>": { "header": "<briefing line>", "lines": [...] } }

import { readFileSync } from "node:fs";
import { join } from "node:path";
import { pathToFileURL } from "node:url";

interface Job {
  readonly name: string;
  readonly team: number;
  readonly steps: string;
}

interface Sheets {
  readonly runSteps: (bundle: unknown, team: number, steps: string) => string[];
  readonly briefingLine: (bundle: unknown, team: number) => string;
}

function isSheets(value: unknown): value is Sheets {
  return (
    typeof value === "object" &&
    value !== null &&
    "runSteps" in value &&
    "briefingLine" in value
  );
}

const source = join(import.meta.dirname, "..", "dist", "briefing", "sheet.js");
const loaded: unknown = await import(pathToFileURL(source).href);
if (!isSheets(loaded)) throw new Error("the built player has no sheet writer");
const request = JSON.parse(readFileSync(process.argv[2] ?? "", "utf8")) as {
  bundle: string;
  jobs: Job[];
};
const bundle: unknown = JSON.parse(readFileSync(request.bundle, "utf8"));
const out: Record<string, { header: string; lines: string[] }> = {};
for (const job of request.jobs) {
  out[job.name] = {
    header: loaded.briefingLine(bundle, job.team),
    lines: loaded.runSteps(bundle, job.team, job.steps),
  };
}
process.stdout.write(JSON.stringify(out));
