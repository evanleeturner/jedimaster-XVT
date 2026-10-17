import { AxeBuilder } from "@axe-core/playwright";
import type { Page } from "@playwright/test";

import {
  callFromSecondSocket,
  expect,
  framedTest as test,
  openMenu,
  routePilot,
  showMission,
} from "./launcher.ts";

const AXE_TAGS = ["wcag2a", "wcag2aa", "wcag21aa", "wcag22aa"];
const MENUS = [
  "Pilot",
  "Missions",
  "Battle",
  "Debriefing",
  "Game options",
  "Status",
  "Settings",
];

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

function near(actual: Box, wanted: Box): void {
  for (const key of ["x", "y", "width", "height"] as const) {
    expect(Math.abs(actual[key] - wanted[key]), key).toBeLessThanOrEqual(1.5);
  }
}

function frameRequests(page: Page): string[] {
  const seen: string[] = [];
  page.on("request", (request) => {
    const match = /\/art\/frame\/(frame-\w+\.png)$/.exec(request.url());
    if (match?.[1] !== undefined) seen.push(match[1]);
  });
  return seen;
}

for (const scale of [1, 2, 3]) {
  test(`framed at ${String(scale)}x: the frame and the map sit at the scaled places`, async ({
    page,
    launcher,
  }) => {
    await page.setViewportSize({ width: 640 * scale, height: 480 * scale });
    await openFramed(page, launcher.url);
    await showMission(page, "First Flight");
    await expect(page.locator("#briefing-canvas")).toBeVisible();
    await page.keyboard.press("Escape");
    near(await boxOf(page, "#frame-art"), {
      x: 0,
      y: 0,
      width: 640 * scale,
      height: 480 * scale,
    });
    near(await boxOf(page, "#briefing-stage"), {
      x: 84 * scale,
      y: 116 * scale,
      width: 360 * scale,
      height: 240 * scale,
    });
    near(await boxOf(page, "#briefing-canvas"), {
      x: 84 * scale,
      y: 116 * scale,
      width: 360 * scale,
      height: 240 * scale,
    });
    const tab = await boxOf(page, ".menu-btn[aria-controls=panel-missions]");
    expect(Math.abs(tab.x + tab.width / 2 - 104 * scale)).toBeLessThanOrEqual(
      1.5,
    );
    expect(Math.abs(tab.y - 26 * scale)).toBeLessThanOrEqual(1.5);
    expect(Math.abs(tab.height - 24 * scale)).toBeLessThanOrEqual(1.5);
  });
}

test("framed: the frame is centred with black around it", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1400, height: 1000 });
  await openFramed(page, launcher.url);
  near(await boxOf(page, "#frame-art"), {
    x: 60,
    y: 20,
    width: 1280,
    height: 960,
  });
  const color = await page.evaluate(
    () => getComputedStyle(document.body).backgroundColor,
  );
  expect(color).toBe("rgb(0, 0, 0)");
});

test("framed: engine fit shows the largest size that fits, smoothed", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1000, height: 1000 });
  await openFramed(page, launcher.url);
  await expect(page.locator("#frame-art")).toHaveCSS(
    "image-rendering",
    "pixelated",
  );
  await openMenu(page, "Settings");
  await page.getByLabel("Engine fit").check();
  await expect(page.locator("#frame-art")).toHaveCSS("image-rendering", "auto");
  near(await boxOf(page, "#frame-art"), {
    x: 0,
    y: 125,
    width: 1000,
    height: 750,
  });
});

test("plain at 639 px wide: no frame, nothing from the skin", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 639, height: 900 });
  await page.goto(launcher.url);
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  await expect(page.locator("html")).not.toHaveClass(/framed/);
  await expect(page.locator("#frame-art")).toBeHidden();
  await expect(page.locator("#options-copy")).toBeHidden();
  await expect(page.locator("#bar-pilot")).toBeHidden();
});

test("plain at 320 px wide: nothing scrolls sideways", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 320, height: 700 });
  await page.goto(launcher.url);
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  await showMission(page, "First Flight");
  await expect(page.locator("#briefing-canvas")).toBeVisible();
  await expect(page.locator("html")).not.toHaveClass(/framed/);
  const wide = await page.evaluate(
    () =>
      document.documentElement.scrollWidth >
      document.documentElement.clientWidth,
  );
  expect(wide).toBe(false);
});

test("plain when the window is high enough but too short", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1200, height: 479 });
  await page.goto(launcher.url);
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  await expect(page.locator("html")).not.toHaveClass(/framed/);
});

test("plain when the frame art answers 404, and framed again once it loads", async ({
  page,
  launcher,
}) => {
  await page.route("**/art/frame/**", (route) =>
    route.fulfill({ status: 404, body: "no" }),
  );
  await page.setViewportSize({ width: 1280, height: 960 });
  await page.goto(launcher.url);
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  await expect(page.locator("html")).not.toHaveClass(/framed/);
  await expect(page.locator("#frame-art")).toBeHidden();
  await page.unroute("**/art/frame/**");
  await showMission(page, "First Flight");
  await expect(page.locator("html")).toHaveClass(/framed/);
});

test("the page stays plain while the frame art is still loading", async ({
  page,
  launcher,
}) => {
  let release: () => void = () => undefined;
  const held = new Promise<void>((resolve) => {
    release = resolve;
  });
  await page.route("**/art/frame/**", async (route) => {
    await held;
    await route.continue();
  });
  await page.setViewportSize({ width: 1280, height: 960 });
  await page.goto(launcher.url, { waitUntil: "domcontentloaded" });
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  await expect(page.locator("html")).not.toHaveClass(/framed/);
  await expect(page.locator("#frame-art")).toBeHidden();
  release();
  await expect(page.locator("html")).toHaveClass(/framed/);
});

test("a picture that failed is asked for again when its background comes back", async ({
  page,
  launcher,
}) => {
  const seen = frameRequests(page);
  let failed = false;
  await page.route("**/art/frame/frame-gametr.png", async (route) => {
    if (failed) {
      await route.continue();
      return;
    }
    failed = true;
    await route.fulfill({ status: 404, body: "no" });
  });
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  await showMission(page, "First Flight");
  await expect.poll(() => seen).toContain("frame-gametr.png");
  await expect(page.locator("html")).not.toHaveClass(/framed/);
  await showMission(page, "Skirmish", "Combat");
  await expect(page.locator("html")).toHaveClass(/framed/);
  await showMission(page, "First Flight");
  await expect(page.locator("#shown-title")).toHaveText("First Flight");
  await expect
    .poll(() => seen.filter((name) => name === "frame-gametr.png").length)
    .toBe(2);
  await expect(page.locator("html")).toHaveClass(/framed/);
  const drawn = await page.evaluate(() => {
    const canvas = document.querySelector<HTMLCanvasElement>("#frame-art");
    const data = canvas?.getContext("2d")?.getImageData(300, 300, 1, 1).data;
    return data === undefined ? 0 : data[3];
  });
  expect(drawn).toBe(255);
});

test("plain under forced colors", async ({ page, launcher }) => {
  await page.emulateMedia({ forcedColors: "active" });
  await page.setViewportSize({ width: 1280, height: 960 });
  await page.goto(launcher.url);
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
  await expect(page.locator("html")).not.toHaveClass(/framed/);
});

test("the frame follows the window: plain when it shrinks, framed when it grows", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  await page.setViewportSize({ width: 600, height: 900 });
  await expect(page.locator("html")).not.toHaveClass(/framed/);
  await page.setViewportSize({ width: 640, height: 480 });
  await expect(page.locator("html")).toHaveClass(/framed/);
});

for (const name of MENUS) {
  test(`axe finds nothing framed with ${name} open`, async ({
    page,
    launcher,
  }) => {
    await page.setViewportSize({ width: 1280, height: 960 });
    await openFramed(page, launcher.url);
    await showMission(page, "First Flight");
    await expect(page.locator("#briefing-canvas")).toBeVisible();
    await openMenu(page, name);
    const result = await new AxeBuilder({ page }).withTags(AXE_TAGS).analyze();
    expect(result.violations).toEqual([]);
  });
}

test("axe finds nothing framed with no menu open and no mission shown", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  const result = await new AxeBuilder({ page }).withTags(AXE_TAGS).analyze();
  expect(result.violations).toEqual([]);
});

for (const name of MENUS) {
  test(`framed: Escape closes ${name} and focus returns to its button`, async ({
    page,
    launcher,
  }) => {
    await page.setViewportSize({ width: 1280, height: 960 });
    await openFramed(page, launcher.url);
    const button = page.locator(".menu-btn", { hasText: name });
    await button.focus();
    await page.keyboard.press("Enter");
    await expect(button).toHaveAttribute("aria-expanded", "true");
    await page.keyboard.press("Escape");
    await expect(button).toHaveAttribute("aria-expanded", "false");
    await expect(button).toBeFocused();
  });
}

test("framed: the options copy is out of the accessibility tree and holds nothing focusable", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  const copy = page.locator("#options-copy");
  await expect(copy).toHaveAttribute("aria-hidden", "true");
  await expect(copy.locator("#options-copy-grid > *")).toHaveCount(8);
  expect(
    await copy
      .locator(
        "a[href], button, input, select, textarea, summary, [tabindex], [contenteditable]",
      )
      .count(),
  ).toBe(0);
  const snapshot = await page.locator("body").ariaSnapshot();
  expect(snapshot).not.toContain("Difficulty:");
  const rows = await page.evaluate(() => {
    const read = (selector: string): string[] =>
      Array.from(document.querySelectorAll(selector)).map((e) => e.textContent);
    return {
      labels: read("#options-list dt"),
      values: read("#options-list dd"),
      copyLabels: read(".options-copy-label"),
      copyValues: read(".options-copy-value"),
    };
  });
  expect(rows.values.length).toBe(8);
  expect(rows.copyValues).toEqual(rows.values);
  expect(rows.copyLabels).toEqual(rows.labels.map((label) => `${label}:`));
});

test("framed: the frame picture is out of the accessibility tree", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  await expect(page.locator("#frame-art")).toHaveAttribute(
    "aria-hidden",
    "true",
  );
  expect(await page.locator("#frame-art").getAttribute("tabindex")).toBeNull();
});

test("framed: text over a white picture keeps 4.5:1 on the plate", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  const plate = await page
    .locator(".menu-btn[aria-controls=panel-missions]")
    .evaluate((e) => getComputedStyle(e).backgroundColor);
  const found = /^rgba?\((\d+), (\d+), (\d+)(?:, ([\d.]+))?\)$/.exec(plate);
  if (found === null) throw new Error(`unreadable plate ${plate}`);
  const alpha = Number(found[4] ?? "1");
  const over = [found[1], found[2], found[3]].map(
    (c) => alpha * Number(c) + (1 - alpha) * 255,
  );
  const light = (rgb: readonly number[]): number => {
    const [r = 0, g = 0, b = 0] = rgb.map((c) => {
      const v = c / 255;
      return v <= 0.03928 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4;
    });
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
  };
  const ground = light(over);
  // The texts the style sheet's header names: white, yellow, cyan, muted.
  for (const text of [
    [255, 255, 255],
    [0xf8, 0xec, 0x48],
    [0x58, 0xd8, 0xf0],
    [0x9f, 0xb8, 0xc0],
  ]) {
    const ratio = (light(text) + 0.05) / (ground + 0.05);
    expect(ratio, `text ${text.join(",")}`).toBeGreaterThanOrEqual(4.5);
  }
});

test("framed: every control is at least 24 x 24 CSS px at 1x", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 640, height: 480 });
  await openFramed(page, launcher.url);
  await showMission(page, "First Flight");
  await expect(page.locator("#briefing-canvas")).toBeVisible();
  await page.keyboard.press("Escape");
  const small = await page.evaluate(() =>
    Array.from(
      document.querySelectorAll(
        ".screen button:not([hidden]), .screen select, .screen input",
      ),
    )
      .filter((e) => e.getClientRects().length > 0)
      .map((e) => {
        const box = e.getBoundingClientRect();
        return {
          id: e.id || e.textContent,
          width: box.width,
          height: box.height,
        };
      })
      .filter((e) => e.width < 23.5 || e.height < 23.5),
  );
  expect(small).toEqual([]);
});

test("the frame's request follows the shown mission and team", async ({
  page,
  launcher,
}) => {
  const seen = frameRequests(page);
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  await expect.poll(() => seen).toEqual(["frame-gametrn.png"]);
  await showMission(page, "First Flight");
  await expect(page.locator("#briefing-canvas")).toBeVisible();
  await expect.poll(() => seen).toContain("frame-gametr.png");
  await showMission(page, "Skirmish", "Combat");
  await expect(page.locator("#shown-title")).toHaveText("Skirmish");
  await expect(page.locator("#briefing-team")).toHaveValue("0");
  await expect.poll(() => seen).toContain("frame-gameci.png");
  await page.locator("#briefing-team").selectOption("1");
  await expect.poll(() => seen).toContain("frame-gamecr.png");
  await page.locator("#briefing-team").selectOption("0");
  await expect(page.locator("#briefing-team")).toHaveValue("0");
  expect(seen).toEqual([
    "frame-gametrn.png",
    "frame-gametr.png",
    "frame-gamecn.png",
    "frame-gameci.png",
    "frame-gamecr.png",
  ]);
});

test("framed at 1x: no text in the controls row or the options copy is clipped", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 640, height: 480 });
  await openFramed(page, launcher.url);
  await showMission(page, "First Flight");
  await expect(page.locator("#briefing-team")).toBeVisible();
  await expect(page.locator("#briefing-team option").first()).toHaveText(
    "Team 0: Imaginary Wing A",
  );
  await expect(page.locator(".options-copy-item")).toHaveCount(8);
  // A label of 26 characters, longer than the install's 24, must fit too.
  await page
    .locator("#briefing-team option")
    .first()
    .evaluate((option) => {
      option.textContent = "Team 0: Imaginary Wing A!!";
    });
  const clipped = await page.evaluate(() => {
    const roots = [
      document.querySelector(".briefing-controls"),
      document.querySelector("#options-copy"),
    ];
    const found: string[] = [];
    for (const root of roots) {
      if (root === null) continue;
      for (const e of [root, ...Array.from(root.querySelectorAll("*"))]) {
        if (!(e instanceof HTMLElement) || e.getClientRects().length === 0)
          continue;
        // The row's own scroll box includes the picker drawn in the right
        // panel; the picker and every other child are checked below.
        if (e.classList.contains("briefing-controls")) continue;
        const box = e.getBoundingClientRect();
        if (box.width < 2 || box.height < 2) continue;
        const style = getComputedStyle(e);
        if (
          e.scrollWidth > e.clientWidth ||
          style.textOverflow === "ellipsis"
        ) {
          found.push(e.className || e.tagName);
        }
      }
    }
    const row = document.querySelector(".briefing-controls");
    const rowRight = row?.getBoundingClientRect().right ?? 0;
    for (const child of Array.from(row?.querySelectorAll("*") ?? [])) {
      const right = child.getBoundingClientRect().right;
      if (child.id === "briefing-team" || child.closest("#briefing-team-box"))
        continue;
      if (right > rowRight + 0.5) found.push(`past the row: ${child.id}`);
    }
    const picker = document.querySelector("#briefing-team");
    if (picker instanceof HTMLSelectElement) {
      const probe = document.createElement("span");
      probe.style.cssText =
        "position:absolute;visibility:hidden;white-space:nowrap";
      probe.textContent = picker.selectedOptions[0]?.textContent ?? "";
      picker.after(probe);
      const style = getComputedStyle(picker);
      probe.style.font = style.font;
      const room =
        picker.clientWidth -
        parseFloat(style.paddingLeft) -
        parseFloat(style.paddingRight) -
        20;
      if (probe.getBoundingClientRect().width > room) found.push("picker text");
      probe.remove();
      const at = picker.getBoundingClientRect();
      if (at.left < 456 || at.right > 622 || at.top < 114 || at.bottom > 362)
        found.push("picker outside the right panel");
      if (at.height < 23.5) found.push("picker under 24 px");
    }
    const panel = document.querySelector("#options-copy");
    const rowBox = row?.getBoundingClientRect();
    const panelBox = panel?.getBoundingClientRect();
    if (rowBox && rowBox.right > 84 + 366 + 1) found.push("row too wide");
    if (panelBox && panelBox.bottom > 438)
      found.push(
        `copy below the panel: ${String(panelBox.bottom)} ${String(panelBox.top)}`,
      );
    return found;
  });
  expect(clipped).toEqual([]);
});

test("framed: the bottom bar names the pilot and follows a rename", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  const push = await routePilot(page, null);
  await openFramed(page, launcher.url);
  const bar = page.locator("#bar-pilot");
  await expect(bar).toBeVisible();
  await expect(bar).toHaveText("Pilot: none yet");
  push({ event: "pilot.changed", data: { pilot: "Wedge" } });
  await expect(bar).toHaveText("Pilot: Wedge");
  push({ event: "pilot.changed", data: { pilot: "Biggs" } });
  await expect(bar).toHaveText("Pilot: Biggs");
  push({ event: "pilot.changed", data: { pilot: null } });
  await expect(bar).toHaveText("Pilot: none yet");
});

test("framed: the pilot's name is in the bar at connect", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await routePilot(page, "Wedge");
  await openFramed(page, launcher.url);
  await expect(page.locator("#bar-pilot")).toHaveText("Pilot: Wedge");
});

test("framed: a change to the options shows in the copy under the map at once", async ({
  page,
  launcher,
  context,
}) => {
  await page.setViewportSize({ width: 1280, height: 960 });
  await openFramed(page, launcher.url);
  const values = page.locator(".options-copy-value");
  await expect(values).toHaveText([
    "Medium",
    "On",
    "On",
    "Off",
    "3 wins",
    "The mission's own limit",
    "Continue",
    "None",
  ]);
  await callFromSecondSocket(context, launcher.origin, "options.set", {
    player: "7001",
    name: "difficulty",
    value: "hard",
  });
  await callFromSecondSocket(context, launcher.origin, "options.set", {
    player: "7001",
    name: "battle_length",
    value: "4",
  });
  await expect(values.first()).toHaveText("Hard");
  await expect(values.nth(4)).toHaveText("4 wins");
  await expect(page.locator("#options-list dd").first()).toHaveText("Hard");
});
