import { AxeBuilder } from "@axe-core/playwright";
import type { Locator, Page } from "@playwright/test";

import {
  expect,
  framedTest as test,
  openMenu,
  showMission,
} from "./launcher.ts";

const AXE_TAGS = ["wcag2a", "wcag2aa", "wcag21aa", "wcag22aa"];
const PLACES: readonly (readonly [string, string, number])[] = [
  ["training", "Training", 114],
  ["melee", "Melee", 142],
  ["tournament", "Tournament", 170],
  ["combat", "Combat engagement", 198],
  ["battle", "Battle", 226],
  ["campaign", "Campaign", 254],
  ["rebel", "Rebel pilot", 306],
  ["imperial", "Imperial pilot", 334],
];

function side(page: Page, id: string): Locator {
  return page.locator(`.side-btn[data-side="${id}"]`);
}

async function openFramed(page: Page, url: string): Promise<void> {
  // A press needs the six solo lists (training, combat and campaign, for
  // each side), which the launcher answers after the page opens.
  let lists = 0;
  page.on("websocket", (socket) => {
    socket.on("framereceived", (frame) => {
      const text = String(frame.payload);
      if (text.includes('"side"') && text.includes('"entries"')) lists += 1;
    });
  });
  await page.goto(url);
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  await expect(page.locator("html")).toHaveClass(/framed/);
  await expect.poll(() => lists).toBeGreaterThanOrEqual(6);
  await page.evaluate(() => 0);
}

async function pressed(page: Page): Promise<string[]> {
  return page.evaluate(() =>
    Array.from(document.querySelectorAll(".side-btn"))
      .filter((b) => b.getAttribute("aria-pressed") === "true")
      .map((b) => b.getAttribute("data-side") ?? ""),
  );
}

async function shownTitle(page: Page): Promise<string> {
  return (await page.locator("#shown-title").textContent()) ?? "";
}

function sideRequests(page: Page): string[] {
  const seen: string[] = [];
  page.on("request", (request) => {
    const match = /\/art\/side\/(\w+\.png)$/.exec(request.url());
    if (match?.[1] !== undefined) seen.push(match[1]);
  });
  return seen;
}

function frameRequests(page: Page): string[] {
  const seen: string[] = [];
  page.on("request", (request) => {
    const match = /\/art\/frame\/(frame-\w+\.png)$/.exec(request.url());
    if (match?.[1] !== undefined) seen.push(match[1]);
  });
  return seen;
}

async function tabTo(page: Page, id: string): Promise<void> {
  for (let n = 0; n < 60; n += 1) {
    await page.keyboard.press("Tab");
    const at = await page.evaluate(
      () => document.activeElement?.getAttribute("data-side") ?? "",
    );
    if (at === id) return;
  }
  throw new Error(`Tab never reached the ${id} button`);
}

for (const scale of [1, 2]) {
  test(`framed at ${String(scale)}x: the eight buttons sit at their places`, async ({
    page,
    launcher,
  }) => {
    await page.setViewportSize({ width: 640 * scale, height: 480 * scale });
    await openFramed(page, launcher.url);
    for (const [id, name, top] of PLACES) {
      const box = await side(page, id).boundingBox();
      expect(box, name).not.toBeNull();
      expect(Math.abs((box?.x ?? 0) - 20 * scale), name).toBeLessThanOrEqual(
        1.5,
      );
      expect(Math.abs((box?.y ?? 0) - top * scale), name).toBeLessThanOrEqual(
        1.5,
      );
      expect(box?.width, name).toBeGreaterThanOrEqual(24 * scale - 1.5);
      expect(box?.height, name).toBeGreaterThanOrEqual(24 * scale - 1.5);
      await expect(side(page, id)).toHaveAccessibleName(name);
    }
    await expect(
      page.getByRole("group", { name: "Mission type" }).getByRole("button"),
    ).toHaveCount(6);
    await expect(
      page.getByRole("group", { name: "Pilot side" }).getByRole("button"),
    ).toHaveCount(2);
    expect(await pressed(page)).toEqual(["rebel"]);
  });
}

test("a type click shows that kind's mission and presses its button", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  await side(page, "training").click();
  await expect(page.locator("#shown-title")).toHaveText("First Flight");
  expect(await pressed(page)).toEqual(["training", "rebel"]);
  await expect(page.locator("#mission-shown")).toHaveText(
    "Showing First Flight",
  );
  await side(page, "combat").click();
  await expect(page.locator("#shown-title")).toHaveText("Skirmish");
  expect(await pressed(page)).toEqual(["combat", "rebel"]);
  // Back to Training: its remembered mission returns.
  await side(page, "training").click();
  await expect(page.locator("#shown-title")).toHaveText("First Flight");
});

test("the pictures follow the buttons: pressed look and lights", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  const seen = sideRequests(page);
  await openFramed(page, launcher.url);
  await side(page, "combat").click();
  await expect(page.locator("#shown-title")).toHaveText("Skirmish");
  const visible = async (): Promise<string[]> =>
    page.evaluate(() =>
      Array.from(
        document.querySelectorAll<HTMLImageElement>("#side-sprites img"),
      )
        .filter((img) => !img.hidden)
        .map((img) => img.src.split("/").pop() ?? "")
        .sort(),
    );
  expect(await visible()).toEqual(
    [
      "game1u.png",
      "game2u.png",
      "game3u.png",
      "game4d.png",
      "game5u.png",
      "game8u.png",
      "reg6d.png",
      "reg7u.png",
      "active1.png",
      "active2.png",
      "active3.png",
      "active5.png",
      "active7.png",
      "active8.png",
      "4lita1.png",
      "4litb1.png",
      "7litb1.png",
    ].sort(),
  );
  expect(seen).toContain("game4d.png");
  await expect(page.locator("#side-sprites")).toHaveAttribute(
    "aria-hidden",
    "true",
  );
  expect(
    await page
      .locator("#side-sprites img")
      .evaluateAll((all) => all.every((img) => img.getAttribute("alt") === "")),
  ).toBe(true);
});

test("a repeat steps through the whole solo list and wraps", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  await side(page, "combat").click();
  await expect(page.locator("#shown-title")).toHaveText("Skirmish");
  // The rebel list is Skirmish, Ghost Fight (locked in the network menu),
  // Raid, Patrol and Ridge Run (not in the network menu at all).
  for (const title of [
    "Ghost Fight",
    "Raid",
    "Patrol",
    "Ridge Run",
    "Skirmish",
  ]) {
    await side(page, "combat").click();
    await expect(page.locator("#shown-title")).toHaveText(title);
  }
});

test("a right click and Shift step back", async ({ page, launcher }) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  await side(page, "combat").click();
  await expect(page.locator("#shown-title")).toHaveText("Skirmish");
  // Back from the first mission wraps to the last one.
  await side(page, "combat").click({ button: "right" });
  await expect(page.locator("#shown-title")).toHaveText("Ridge Run");
  await side(page, "combat").click({ modifiers: ["Shift"] });
  await expect(page.locator("#shown-title")).toHaveText("Patrol");
  await side(page, "combat").click({ button: "right" });
  await expect(page.locator("#shown-title")).toHaveText("Raid");
});

test("the solo lists reach a file the network menu lacks and one it marks not available", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  const team = page.locator("#briefing-team option").first();
  await side(page, "combat").click();
  await expect(page.locator("#shown-title")).toHaveText("Skirmish");
  // Ghost Fight: the network menu marks it not available.
  await side(page, "combat").click();
  await expect(page.locator("#shown-title")).toHaveText("Ghost Fight");
  await expect(team).toHaveText("Team 0: Ghost Wing A");
  // Ridge Run: no network menu entry at all.
  for (const title of ["Raid", "Patrol", "Ridge Run"]) {
    await side(page, "combat").click();
    await expect(page.locator("#shown-title")).toHaveText(title);
  }
  await expect(team).toHaveText("Team 0: Ridge Wing A");
  expect(await pressed(page)).toEqual(["combat", "rebel"]);
  // Training: the third solo mission is in no network menu entry either.
  await side(page, "training").click();
  await expect(page.locator("#shown-title")).toHaveText("First Flight");
  await side(page, "training").click();
  await expect(page.locator("#shown-title")).toHaveText("Third Flight");
  await expect(team).toHaveText("Team 0: Charlie Wing A");
  await expect(page.locator("#mission-shown")).toHaveText(
    "Showing Third Flight",
  );
});

test("the Missions menu still shows by id and marks the solo mission by file", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  await side(page, "combat").click();
  await side(page, "combat").click();
  await expect(page.locator("#shown-title")).toHaveText("Ghost Fight");
  await openMenu(page, "Missions");
  await page
    .locator("#missions-kinds")
    .getByRole("button", { name: /^Combat/ })
    .click();
  // The menu marks the row of the solo mission by its file, though the
  // network menu says that mission is not available and so offers no button.
  await expect(
    page.locator('#missions-list [aria-current="true"]'),
  ).toContainText("Ghost Fight");
  await expect(page.getByRole("button", { name: /^Show Ghost/ })).toHaveCount(
    0,
  );
  // A pick from the menu goes by id; the buttons follow it.
  await page.getByRole("button", { name: "Show Sweep", exact: true }).click();
  await expect(page.locator("#shown-title")).toHaveText("Sweep");
  expect(await pressed(page)).toEqual(["combat", "imperial"]);
  await showMission(page, "Raid", "Combat");
  await expect(page.locator("#shown-title")).toHaveText("Raid");
  expect(await pressed(page)).toEqual(["combat", "imperial"]);
});

test("a right click on a button leaves the browser's menu out", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  const prevented = await side(page, "melee").evaluate(
    (element) =>
      new Promise<boolean>((resolve) => {
        const event = new MouseEvent("contextmenu", {
          bubbles: true,
          cancelable: true,
        });
        element.dispatchEvent(event);
        resolve(event.defaultPrevented);
      }),
  );
  expect(prevented).toBe(true);
});

test("a side switch shows the other list and the background follows", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  const frames = frameRequests(page);
  await openFramed(page, launcher.url);
  await side(page, "combat").click();
  await expect(page.locator("#shown-title")).toHaveText("Skirmish");
  await side(page, "combat").click();
  await expect(page.locator("#shown-title")).toHaveText("Ghost Fight");
  await side(page, "imperial").click();
  await expect(page.locator("#shown-title")).toHaveText("Sweep");
  expect(await pressed(page)).toEqual(["combat", "imperial"]);
  await expect.poll(() => frames).toContain("frame-gameci.png");
  // The chosen side's own button does nothing.
  await side(page, "imperial").click();
  await expect(page.locator("#shown-title")).toHaveText("Sweep");
  // The rebel list returns to the mission it was left on.
  await side(page, "rebel").click();
  await expect(page.locator("#shown-title")).toHaveText("Ghost Fight");
  expect(await pressed(page)).toEqual(["combat", "rebel"]);
  expect(frames.filter((f) => f === "frame-gamecr.png")).toHaveLength(1);
  expect(frames.filter((f) => f === "frame-gameci.png")).toHaveLength(1);
});

test("a mission shown from the Missions menu moves the buttons with it", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  await showMission(page, "Sweep", "Combat");
  await expect(page.locator("#shown-title")).toHaveText("Sweep");
  expect(await pressed(page)).toEqual(["combat", "imperial"]);
  await showMission(page, "First Flight");
  await expect(page.locator("#shown-title")).toHaveText("First Flight");
  expect(await pressed(page)).toEqual(["training", "rebel"]);
});

test("a type with nothing available is selected and the page says so", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  await side(page, "training").click();
  await expect(page.locator("#shown-title")).toHaveText("First Flight");
  await side(page, "tournament").click();
  expect(await pressed(page)).toEqual(["tournament", "rebel"]);
  await expect(page.locator("#side-note")).toHaveText(
    "No Tournament mission is available to show.",
  );
  expect(await shownTitle(page)).toBe("First Flight");
  await side(page, "training").click();
  await expect(page.locator("#side-note")).toHaveText("");
});

test("the keyboard alone does all of it", async ({ page, launcher }) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  await tabTo(page, "combat");
  await expect(side(page, "combat")).toBeFocused();
  await page.keyboard.press("Enter");
  await expect(page.locator("#shown-title")).toHaveText("Skirmish");
  await expect(side(page, "combat")).toHaveAttribute(
    "aria-describedby",
    "side-step-hint",
  );
  await page.keyboard.press("Space");
  await expect(page.locator("#shown-title")).toHaveText("Ghost Fight");
  await page.keyboard.press("Shift+Enter");
  await expect(page.locator("#shown-title")).toHaveText("Skirmish");
  await page.keyboard.press("Tab");
  await page.keyboard.press("Tab");
  await page.keyboard.press("Tab");
  await expect(side(page, "rebel")).toBeFocused();
  await page.keyboard.press("Tab");
  await expect(side(page, "imperial")).toBeFocused();
  await page.keyboard.press("Enter");
  await expect(page.locator("#shown-title")).toHaveText("Sweep");
  expect(await pressed(page)).toEqual(["combat", "imperial"]);
});

test("the tooltip shows on hover and on focus, on a dark plate", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 640, height: 480 });
  await openFramed(page, launcher.url);
  const tip = side(page, "combat").locator(".side-tip");
  await expect(tip).toBeHidden();
  await side(page, "combat").hover();
  await expect(tip).toBeVisible();
  await expect(tip).toHaveText("Combat engagement");
  await page.mouse.move(600, 460);
  await expect(tip).toBeHidden();
  await tabTo(page, "melee");
  await expect(side(page, "melee").locator(".side-tip")).toBeVisible();
  const plate = await tip.evaluate(
    (el) => getComputedStyle(el).backgroundColor,
  );
  expect(plate).toMatch(/^rgba\(10, 6, 44, 0\.9[0-9]*\)$/);
});

test("axe finds nothing with the side buttons framed", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  await side(page, "combat").click();
  await expect(page.locator("#shown-title")).toHaveText("Skirmish");
  await side(page, "combat").hover();
  const results = await new AxeBuilder({ page }).withTags(AXE_TAGS).analyze();
  expect(results.violations).toEqual([]);
});

test("below 640 x 480 the buttons are gone from the page and the tab order", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 639, height: 480 });
  await page.goto(launcher.url);
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  await expect(page.locator("html")).not.toHaveClass(/framed/);
  await expect(page.locator(".side-btn:visible")).toHaveCount(0);
  for (let n = 0; n < 30; n += 1) {
    await page.keyboard.press("Tab");
    const reached = await page.evaluate(
      () => document.activeElement?.classList.contains("side-btn") ?? false,
    );
    expect(reached).toBe(false);
  }
});

test("a side picture that answers 404 does not show and its button still works", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await page.route("**/art/side/game4d.png", (route) =>
    route.fulfill({ status: 404, body: "no" }),
  );
  await openFramed(page, launcher.url);
  await side(page, "combat").click();
  await expect(page.locator("#shown-title")).toHaveText("Skirmish");
  await expect(side(page, "combat")).toHaveAttribute("aria-pressed", "true");
  await expect(
    page.locator('#side-sprites img[src$="game4d.png"]'),
  ).toBeHidden();
  await expect(
    page.locator('#side-sprites img[src$="reg6d.png"]'),
  ).toBeVisible();
  await side(page, "combat").click();
  await expect(page.locator("#shown-title")).toHaveText("Ghost Fight");
});
