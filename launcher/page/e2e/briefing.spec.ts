import { AxeBuilder } from "@axe-core/playwright";
import type { Locator, Page } from "@playwright/test";

import { expect, test } from "./launcher.ts";

const AXE_TAGS = ["wcag2a", "wcag2aa", "wcag21aa", "wcag22aa"];
const CAPTION = "Intro Brief words here to read.";

async function openBriefing(page: Page, url: string): Promise<void> {
  await page.goto(url);
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  await page.getByRole("button", { name: "Show First Flight" }).click();
  await expect(page.locator("#briefing-canvas")).toBeVisible();
  await expect(page.locator("#briefing-caption")).toHaveText(CAPTION);
}

function control(page: Page, name: string): Locator {
  return page.getByRole("button", { name, exact: true });
}

async function offered(button: Locator): Promise<boolean> {
  return (await button.getAttribute("aria-disabled")) === "false";
}

async function paintedColors(page: Page): Promise<string[]> {
  return page.evaluate(() => {
    const canvas = document.querySelector("canvas");
    const context = canvas?.getContext("2d");
    if (!canvas || !context) return [];
    const data = context.getImageData(0, 0, canvas.width, canvas.height).data;
    const seen = new Set<string>();
    for (let at = 0; at < data.length; at += 4) {
      seen.add(
        `${String(data[at])},${String(data[at + 1])},${String(data[at + 2])}`,
      );
    }
    return [...seen];
  });
}

test("a picked mission opens its briefing stopped, with its words for a screen reader", async ({
  page,
  launcher,
}) => {
  await page.goto(launcher.url);
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  await expect(page.locator("#briefing-note")).toHaveText(
    "Pick a mission to see its briefing.",
  );
  await page.getByRole("button", { name: "Show First Flight" }).click();
  const section = page.getByRole("region", { name: "Briefing" });
  await expect(section.getByRole("img")).toBeVisible();
  await expect(section.getByRole("img")).toHaveAttribute(
    "aria-label",
    /Briefing map/,
  );
  await expect(page.locator("#briefing-caption")).toHaveText(CAPTION);
  await expect(page.locator("#briefing-caption")).toHaveAttribute(
    "aria-live",
    "polite",
  );
  await expect(page.locator("#briefing-page")).toHaveText("Page 1");
  await expect(page.locator("#briefing-highlight")).toHaveText(
    "No flight group is highlighted.",
  );
  expect(await offered(control(page, "Play"))).toBe(true);
  expect(await offered(control(page, "Stop"))).toBe(false);
  expect(await offered(control(page, "Rewind"))).toBe(true);
  expect(await offered(control(page, "Forward"))).toBe(false);
  await page.waitForTimeout(400);
  await expect(page.locator("#briefing-highlight")).toHaveText(
    "No flight group is highlighted.",
  );
  await expect(page.getByRole("switch", { name: "Sound" })).toBeChecked();
  await expect(page.getByLabel("Team")).toHaveText(/Team 0: RedTeam 1: Blue/);
});

test("Play, Stop, Rewind and Forward work from the keyboard", async ({
  page,
  launcher,
}) => {
  await openBriefing(page, launcher.url);
  const highlight = page.locator("#briefing-highlight");
  await control(page, "Play").focus();
  await page.keyboard.press("Enter");
  await expect(highlight).toHaveText("Highlighted: Alpha Wing.");
  await expect(page.locator("#briefing-labels li")).toHaveText(["Rally point"]);
  expect(await offered(control(page, "Play"))).toBe(false);
  expect(await offered(control(page, "Stop"))).toBe(true);
  await control(page, "Stop").focus();
  await page.keyboard.press("Space");
  expect(await offered(control(page, "Play"))).toBe(true);
  await control(page, "Rewind").focus();
  await page.keyboard.press("Enter");
  await expect(highlight).toHaveText("No flight group is highlighted.");
  await expect(page.locator("#briefing-labels li")).toHaveCount(0);
  await control(page, "Play").focus();
  await page.keyboard.press("Enter");
  await expect(control(page, "Forward")).toHaveAttribute(
    "aria-disabled",
    "false",
  );
  await control(page, "Forward").focus();
  await page.keyboard.press("Enter");
  await expect(page.locator("#briefing-page")).toHaveText("Page 1");
});

test("a control that is not offered stays reachable and does nothing", async ({
  page,
  launcher,
}) => {
  await openBriefing(page, launcher.url);
  const stop = control(page, "Stop");
  await stop.focus();
  await expect(stop).toBeFocused();
  await page.keyboard.press("Enter");
  expect(await offered(control(page, "Play"))).toBe(true);
  expect(await offered(stop)).toBe(false);
});

test("the team picker opens that team's briefing, stopped", async ({
  page,
  launcher,
}) => {
  await openBriefing(page, launcher.url);
  await control(page, "Play").click();
  await expect(control(page, "Stop")).toHaveAttribute("aria-disabled", "false");
  await page.getByLabel("Team").selectOption("1");
  expect(await offered(control(page, "Play"))).toBe(true);
  await expect(page.locator("#briefing-highlight")).toHaveText(
    "No flight group is highlighted.",
  );
});

test("the map is drawn: the grid's red and an icon's colors are on the canvas", async ({
  page,
  launcher,
}) => {
  await openBriefing(page, launcher.url);
  const colors = await paintedColors(page);
  expect(colors).toContain("148,0,0");
  expect(colors.length).toBeGreaterThan(4);
});

test("whole pixels draws the largest whole multiple, sharp", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 720 });
  await openBriefing(page, launcher.url);
  const canvas = page.locator("#briefing-canvas");
  const box = await canvas.boundingBox();
  expect(box?.width).toBe(720);
  expect(box?.height).toBe(480);
  expect(await canvas.evaluate((c) => getComputedStyle(c).imageRendering)).toBe(
    "pixelated",
  );
  await page.getByLabel("Engine fit").check();
  await expect
    .poll(() => canvas.evaluate((c) => getComputedStyle(c).imageRendering))
    .not.toBe("pixelated");
});

test("with reduced motion the map shows the marker's final box at once", async ({
  page,
  launcher,
}) => {
  const start = async (): Promise<string[]> => {
    await control(page, "Play").click();
    await expect(page.locator("#briefing-highlight")).toHaveText(
      "Highlighted: Alpha Wing.",
    );
    await control(page, "Stop").click();
    return paintedColors(page);
  };
  await openBriefing(page, launcher.url);
  expect(await start()).not.toContain("0,227,0");
  await page.emulateMedia({ reducedMotion: "reduce" });
  await page.reload();
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  await page.getByRole("button", { name: "Show First Flight" }).click();
  await expect(page.locator("#briefing-caption")).toHaveText(CAPTION);
  expect(await start()).toContain("0,227,0");
});

test("a pick from another socket reaches the page the same way", async ({
  page,
  launcher,
  context,
}) => {
  await page.goto(launcher.url);
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  const caller = await context.newPage();
  await caller.goto(`${launcher.origin}/style.css`);
  await caller.evaluate(
    () =>
      new Promise<void>((resolve) => {
        const socket = new WebSocket(`ws://${location.host}/ws`);
        socket.addEventListener("open", () => {
          socket.send(
            JSON.stringify({
              id: 1,
              command: "page.show_mission",
              args: { mission_type: "training", id: 1 },
            }),
          );
        });
        socket.addEventListener("message", (event: MessageEvent<string>) => {
          if ("ok" in (JSON.parse(event.data) as object)) {
            socket.close();
            resolve();
          }
        });
      }),
  );
  await expect(page.locator("#briefing-caption")).toHaveText(CAPTION);
});

test("a mission without a bundle says so", async ({ page, launcher }) => {
  await page.goto(launcher.url);
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  await page.getByRole("button", { name: "Show Open Field" }).click();
  await expect(page.locator("#briefing-note")).toHaveText(
    "This mission's briefing could not be shown.",
  );
  await expect(page.locator("#briefing-canvas")).toBeHidden();
});

test("axe finds nothing with the briefing open, in both schemes", async ({
  page,
  launcher,
}) => {
  await openBriefing(page, launcher.url);
  await control(page, "Play").click();
  await expect(page.locator("#briefing-highlight")).toHaveText(
    "Highlighted: Alpha Wing.",
  );
  for (const scheme of ["light", "dark"] as const) {
    await page.emulateMedia({ colorScheme: scheme });
    const result = await new AxeBuilder({ page }).withTags(AXE_TAGS).analyze();
    expect(result.violations, scheme).toEqual([]);
  }
});

test("the briefing controls are at least 24 by 24 and the page reflows at 320 wide", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 320, height: 640 });
  await openBriefing(page, launcher.url);
  const small = await page.evaluate(() =>
    Array.from(
      document.querySelectorAll(
        "#briefing button, #briefing input, #briefing select",
      ),
    )
      .map((el) => ({ id: el.id, box: el.getBoundingClientRect() }))
      .filter(({ box }) => box.width > 0 && (box.width < 24 || box.height < 24))
      .map(({ id }) => id),
  );
  expect(small).toEqual([]);
  const overflow = await page.evaluate(
    () =>
      document.documentElement.scrollWidth -
      document.documentElement.clientWidth,
  );
  expect(overflow).toBeLessThanOrEqual(0);
});
