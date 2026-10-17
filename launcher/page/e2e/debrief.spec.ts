import { AxeBuilder } from "@axe-core/playwright";
import type { BrowserContext, Page } from "@playwright/test";

import {
  debrief,
  detail,
  player,
  team,
} from "../src/debrief/debrief.fixture.ts";
import {
  callFromSecondSocket,
  expect,
  framedTest as test,
  openMenu,
  routePilot,
  showMission,
} from "./launcher.ts";

const AXE_TAGS = ["wcag2a", "wcag2aa", "wcag21aa", "wcag22aa"];
const TRAINING = {
  player: "7001",
  mission_type: "training",
  side: "rebel",
  mission: "alpha.tie",
  pilot: "Ace",
};
const MELEE = {
  player: "7001",
  mission_type: "melee",
  side: "none",
  mission: "delta.tie",
  pilot: "Ace",
};

interface Box {
  readonly x: number;
  readonly y: number;
  readonly width: number;
  readonly height: number;
}

async function openFramed(page: Page, url: string): Promise<void> {
  await page.goto(url);
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  await expect(page.locator("html")).toHaveClass(/framed/);
}

async function boxOf(page: Page, selector: string): Promise<Box> {
  const box = await page.locator(selector).boundingBox();
  if (box === null) throw new Error(`${selector} has no box`);
  return box;
}

/** A box in the 640 x 480 screen's own pixels (the frame may sit off the window's corner). */
async function onScreen(page: Page, selector: string): Promise<Box> {
  const screen = await boxOf(page, "#panel-debrief");
  const box = await boxOf(page, selector);
  return {
    x: box.x - screen.x,
    y: box.y - screen.y,
    width: box.width,
    height: box.height,
  };
}

function near(actual: Box, wanted: Box, scale: number): void {
  for (const key of ["x", "y", "width", "height"] as const) {
    expect(
      Math.abs(actual[key] - wanted[key] * scale),
      key,
    ).toBeLessThanOrEqual(1.5);
  }
}

function debriefRequests(page: Page): string[] {
  const seen: string[] = [];
  page.on("request", (request) => {
    const match = /\/art\/debrief\/([\w.]+)$/.exec(request.url());
    if (match?.[1] !== undefined) seen.push(match[1]);
  });
  return seen;
}

async function fly(
  context: BrowserContext,
  origin: string,
  args: Record<string, string>,
): Promise<void> {
  const reply = await callFromSecondSocket(
    context,
    origin,
    "launch.solo",
    args,
  );
  expect(reply.result.started).toBe(true);
}

async function drawn(page: Page): Promise<string[]> {
  const raw = await page.locator("#debrief-ink").getAttribute("data-lines");
  const lines = JSON.parse(raw ?? "[]") as { text: string }[];
  return lines.map((l) => l.text);
}

async function cells(page: Page): Promise<string[][]> {
  return page
    .locator("#debrief-page tr")
    .evaluateAll((rows) =>
      rows.map((row) =>
        Array.from(row.querySelectorAll("th, td")).map((c) => c.textContent),
      ),
    );
}

/** The cells that hold a number: a count, a "k (s)" pair, a percentage, a clock. */
function numberCells(rows: readonly (readonly string[])[]): string[] {
  const numeric = /^\d[\d:%() ]*$/;
  return rows.flatMap((row) => row.filter((c) => numeric.test(c)));
}

function expectDrawn(lines: readonly string[], rows: string[][]): void {
  for (const cell of numberCells(rows)) {
    const found = lines.some((t) => t === cell || t.endsWith(` ${cell}`));
    expect(found, `${cell} is drawn`).toBe(true);
  }
}

async function openDebriefing(page: Page): Promise<void> {
  await openMenu(page, "Debriefing");
  await expect(page.locator("html")).toHaveClass(/debrief-screen/);
  await expect(page.locator("#debrief-ink")).toHaveAttribute(
    "data-lines",
    /Mission Score|Score/,
  );
}

for (const scale of [1, 2]) {
  test(`framed at ${String(scale)}x: a flown battle opens as the 1997 screen`, async ({
    page,
    launcher,
    context,
  }) => {
    await launcher.behave("done4");
    await page.setViewportSize({ width: 640 * scale, height: 480 * scale });
    const requests = debriefRequests(page);
    await openFramed(page, launcher.url);
    await fly(context, launcher.origin, TRAINING);
    await expect(page.locator("#debrief-badge")).toBeVisible();
    await openDebriefing(page);
    expect(requests).toContain("debtrwr.png");
    expect(requests).toContain("fonts.json");
    expect(requests).toContain("debrief2u.png");
    expect(requests).toContain("leaveup.png");
    near(
      await boxOf(page, "#panel-debrief"),
      { x: 0, y: 0, width: 640, height: 480 },
      scale,
    );
    near(
      await onScreen(page, "#debrief-ground"),
      { x: 0, y: 0, width: 640, height: 480 },
      scale,
    );
    expect(
      await page
        .locator("#debrief-ground")
        .evaluate((c: HTMLCanvasElement) => c.width),
    ).toBe(640 * scale);
    near(
      await onScreen(page, "#debrief-tab-statistics"),
      { x: 20, y: 142, width: 24, height: 24 },
      scale,
    );
    near(
      await onScreen(page, "#debrief-leave"),
      { x: 85, y: 447, width: 91, height: 24 },
      scale,
    );
    // The game's button pictures are strips drawn from the screen's corner.
    near(
      await onScreen(page, "#debrief-tab-statistics .tab-selected"),
      { x: 0, y: 0, width: 42, height: 166 },
      scale,
    );
    near(
      await onScreen(page, "#debrief-leave .tab-art:first-of-type"),
      { x: 0, y: 0, width: 176, height: 471 },
      scale,
    );
    // A solo training flight offers the statistics page only.
    await expect(page.locator("#debrief-tab-overview")).toBeHidden();
    await expect(page.locator("#debrief-tab-statistics")).toHaveAttribute(
      "aria-selected",
      "true",
    );
    expectDrawn(await drawn(page), await cells(page));
  });
}

test("framed: nothing of the Mission Selection screen stays visible or in the tab order", async ({
  page,
  launcher,
  context,
}) => {
  await launcher.behave("done4");
  await openFramed(page, launcher.url);
  await fly(context, launcher.origin, TRAINING);
  await openDebriefing(page);
  for (const hidden of [
    ".side-btn",
    ".menu-btn",
    "#frame-art",
    ".skip-link",
    "#main",
  ]) {
    await expect(page.locator(hidden).first()).toBeHidden();
  }
  const reached = new Set<string>();
  await page.locator("#debrief-pick").focus();
  for (let n = 0; n < 8; n += 1) {
    await page.keyboard.press("Tab");
    reached.add(await page.evaluate(() => document.activeElement?.id ?? ""));
  }
  expect([...reached].filter((id) => id !== "").sort()).toEqual([
    "debrief-leave",
    "debrief-pick",
    "debrief-scroll-down",
    "debrief-tab-statistics",
  ]);
});

test("framed: the drawn layer is out of the accessibility tree and the tables are in", async ({
  page,
  launcher,
  context,
}) => {
  await launcher.behave("done4");
  await openFramed(page, launcher.url);
  await fly(context, launcher.origin, TRAINING);
  await openDebriefing(page);
  await expect(page.locator(".debrief-art")).toHaveAttribute(
    "aria-hidden",
    "true",
  );
  const panel = page.getByRole("tabpanel");
  await expect(panel.getByRole("row", { name: "Mission Score" })).toContainText(
    "10",
  );
  await expect(
    panel.getByRole("row", { name: "Laser Accuracy" }),
  ).toContainText("30%");
  await expect(
    page.getByRole("button", { name: "Back to the menu" }),
  ).toBeVisible();
  const found = await new AxeBuilder({ page }).withTags(AXE_TAGS).analyze();
  expect(found.violations).toEqual([]);
});

test("framed: a melee opens on the overview; tabs by click and by keyboard alone; the log names the background and each tab", async ({
  page,
  launcher,
  context,
}) => {
  await launcher.behave("done4");
  const logs: string[] = [];
  page.on("console", (message) => logs.push(message.text()));
  const requests = debriefRequests(page);
  await openFramed(page, launcher.url);
  await page.goto(`${launcher.origin}/?log=debug`);
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  await fly(context, launcher.origin, MELEE);
  await expect(page.locator("#debrief-badge")).toBeVisible();
  await openDebriefing(page);
  const overview = page.getByRole("tab", { name: "Mission overview" });
  const statistics = page.getByRole("tab", { name: "Player statistics" });
  await expect(overview).toBeVisible();
  await expect(overview).toHaveAttribute("aria-selected", "true");
  expect(requests).toContain("debmwr.png");
  expect(requests).toContain("debrief1u.png");
  expect(requests).toContain("debrief1d.png");
  near(
    await onScreen(page, "#debrief-tab-overview"),
    { x: 20, y: 114, width: 24, height: 24 },
    1,
  );
  expectDrawn(await drawn(page), await cells(page));
  await overview.focus();
  await page.keyboard.press("ArrowRight");
  await expect(statistics).toBeFocused();
  await expect(statistics).toHaveAttribute("aria-selected", "true");
  await expect(page.locator("#debrief-ink")).toHaveAttribute(
    "data-lines",
    /Total Kills/,
  );
  expectDrawn(await drawn(page), await cells(page));
  await page.keyboard.press("Home");
  await expect(overview).toBeFocused();
  await expect(overview).toHaveAttribute("aria-selected", "true");
  await statistics.click();
  await expect(statistics).toHaveAttribute("aria-selected", "true");
  await overview.click();
  await expect(page.locator("#debrief-ink")).toHaveAttribute(
    "data-lines",
    /Deaths/,
  );
  expectDrawn(await drawn(page), await cells(page));
  const found = await new AxeBuilder({ page }).withTags(AXE_TAGS).analyze();
  expect(found.violations).toEqual([]);
  const text = logs.join("\n");
  expect(text).toMatch(/debriefing background/);
  expect(text).toMatch(/debmwr/);
  expect(text).toMatch(/debriefing tab/);
});

test("framed: leaving returns the screen it came from", async ({
  page,
  launcher,
  context,
}) => {
  await launcher.behave("done4");
  await openFramed(page, launcher.url);
  await showMission(page, "First Flight");
  await expect(page.locator("#briefing-canvas")).toBeVisible();
  await page.keyboard.press("Escape");
  await fly(context, launcher.origin, TRAINING);
  await openDebriefing(page);
  await page.getByRole("button", { name: "Back to the menu" }).click();
  await expect(page.locator("html")).not.toHaveClass(/debrief-screen/);
  await expect(page.locator("#panel-debrief")).toBeHidden();
  await expect(
    page.locator(".menu-btn[aria-controls=panel-debrief]"),
  ).toBeFocused();
  await expect(page.locator("#briefing-canvas")).toBeVisible();
  await expect(page.locator(".side-btn").first()).toBeVisible();
  await openDebriefing(page);
  await page.keyboard.press("Escape");
  await expect(page.locator("html")).not.toHaveClass(/debrief-screen/);
  await expect(
    page.locator(".menu-btn[aria-controls=panel-debrief]"),
  ).toBeFocused();
});

test("plain at 639 px wide: the plain layout, no screen, no debriefing art asked for", async ({
  page,
  launcher,
  context,
}) => {
  await launcher.behave("done4");
  await page.setViewportSize({ width: 639, height: 800 });
  const requests = debriefRequests(page);
  await page.goto(launcher.url);
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  await fly(context, launcher.origin, MELEE);
  await expect(page.locator("#debrief-badge")).toBeVisible();
  await openMenu(page, "Debriefing");
  await expect(page.locator("html")).not.toHaveClass(/framed/);
  await expect(page.locator("html")).not.toHaveClass(/debrief-screen/);
  await expect(
    page.getByRole("tab", { name: "Mission overview" }),
  ).toBeVisible();
  await expect(
    page.getByRole("tab", { name: "Player statistics" }),
  ).toBeVisible();
  await expect(page.locator("#debrief-page table").first()).toBeVisible();
  await expect(page.locator(".debrief-art")).toBeHidden();
  await expect(page.locator("#debrief-leave")).toBeHidden();
  expect(requests).toEqual([]);
});

test("plain under forced colors: no screen", async ({
  page,
  launcher,
  context,
}) => {
  await launcher.behave("done4");
  await page.emulateMedia({ forcedColors: "active" });
  await page.goto(launcher.url);
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  await fly(context, launcher.origin, TRAINING);
  await openMenu(page, "Debriefing");
  // The plain layout opens on the overview, which the screen never offers a solo
  // training flight; the screen decides in that same render, so the class is
  // settled after this.
  await expect(page.locator("#debrief-page table").first()).toContainText(
    "Mission overview",
  );
  await expect(page.locator("html")).not.toHaveClass(/debrief-screen/);
});

test("framed: pictures that answer 404 do not show and the screen still works", async ({
  page,
  launcher,
  context,
}) => {
  await launcher.behave("done4");
  await page.route(/\/art\/debrief\/(deb|leave)\w*\.png$/, (route) =>
    route.fulfill({ status: 404, body: "no" }),
  );
  await openFramed(page, launcher.url);
  await fly(context, launcher.origin, MELEE);
  await openDebriefing(page);
  const statistics = page.getByRole("tab", { name: "Player statistics" });
  await expect(statistics).toBeVisible();
  await expect(page.locator("#debrief-tab-statistics")).toHaveClass(/no-art/);
  await statistics.click();
  await expect(statistics).toHaveAttribute("aria-selected", "true");
  await expect(
    page.getByRole("tabpanel").getByRole("row", { name: "Total Kills" }),
  ).toContainText("2 (0)");
  expectDrawn(await drawn(page), await cells(page));
  await expect(
    page.getByRole("button", { name: "Back to the menu" }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Back to the menu" }).click();
  await expect(page.locator("html")).not.toHaveClass(/debrief-screen/);
});

test("framed: a network battle scrolls its statistics and every drawn number is a table cell", async ({
  page,
  launcher,
  context,
}) => {
  await launcher.behave("done4");
  const send = await routePilot(page, "Ace");
  await openFramed(page, launcher.url);
  await fly(context, launcher.origin, TRAINING);
  await expect(page.locator("#debrief-badge")).toBeVisible();
  const many = Array.from({ length: 6 }, (_, n) => ({
    entry: n + 1,
    name: `Foe ${String(n)}`,
    kills: n + 1,
    shared: n,
  }));
  const rich = debrief({
    result_id: 90,
    mission_type: "combat",
    side: "imperial",
    title: "Network Skirmish (Combat)",
    pilot: "Ace",
    players: [
      player(0, "Ace", 0, 1450, { kills: 5, shared: 1, losses: 1 }),
      player(1, "Vader", 1, 980),
      player(2, "Luke", 0, 1500),
    ],
    teams: [team(0, "Red", 1450, true), team(1, "Blue", 980, false)],
    detail: detail({ on_players: many, from_players: many }),
  });
  send({
    event: "launch.started",
    data: {
      launch_id: 4001,
      mission_type: "combat",
      side: "imperial",
      mission: "skirm.tie",
      title: rich.title,
      pilot: "Ace",
    },
  });
  send({
    event: "launch.ended",
    data: { launch_id: 4001, status: "flown", line: "Flown.", debrief: rich },
  });
  await openMenu(page, "Debriefing");
  await expect(page.locator("html")).toHaveClass(/debrief-screen/);
  await page
    .locator("#debrief-pick")
    .selectOption("90")
    .catch(() => undefined);
  await expect(
    page.getByRole("tab", { name: "Mission overview" }),
  ).toBeVisible();
  await page.getByRole("tab", { name: "Player statistics" }).click();
  await expect(page.locator("#debrief-scroll-down")).toBeVisible();
  await expect(page.locator("#debrief-ink")).toHaveAttribute(
    "data-lines",
    /Mission Score/,
  );
  await expect(page.locator("#debrief-scroll-up")).toBeDisabled();
  const seen = new Set<string>();
  for (let n = 0; n < 4; n += 1) {
    for (const text of await drawn(page)) seen.add(text);
    if (await page.locator("#debrief-scroll-down").isDisabled()) break;
    await page.locator("#debrief-scroll-down").click();
  }
  await expect(page.locator("#debrief-scroll-up")).toBeEnabled();
  expectDrawn([...seen], await cells(page));
  const found = await new AxeBuilder({ page }).withTags(AXE_TAGS).analyze();
  expect(found.violations).toEqual([]);
});
