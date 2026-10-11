import { AxeBuilder } from "@axe-core/playwright";
import type { BrowserContext, Page } from "@playwright/test";

import { expect, test } from "./launcher.ts";

const AXE_TAGS = ["wcag2a", "wcag2aa", "wcag21aa", "wcag22aa"];

async function openPage(page: Page, url: string): Promise<void> {
  await page.goto(url);
  await expect(page.getByText("Connected to the launcher.")).toBeVisible();
  await expect(page.getByText("Launcher version: 0.1.0")).toBeVisible();
  await expect(
    page.getByText("Training missions", { exact: true }),
  ).toBeVisible();
  await expect(page.getByLabel("Whole pixels")).toBeEnabled();
}

test("the key opens the page and is gone from the address", async ({
  page,
  launcher,
}) => {
  await openPage(page, launcher.url);
  expect(page.url()).toBe(`${launcher.origin}/`);
  await expect(page).toHaveTitle("jedimaster launcher");
  await expect(page.getByRole("heading", { level: 1 })).toHaveText(
    "jedimaster launcher",
  );
  expect(await page.locator("html").getAttribute("lang")).toBe("en");
});

test("the page is refused without the key, with a wrong one, or a wrong host", async ({
  browser,
  launcher,
}) => {
  const context = await browser.newContext();
  const page = await context.newPage();
  const bare = await page.goto(`${launcher.origin}/`);
  expect(bare?.status()).toBe(403);
  const wrong = await page.goto(`${launcher.origin}/?key=wrong`);
  expect(wrong?.status()).toBe(403);
  const hosted = await context.request.get(
    `${launcher.origin}/?key=${launcher.key}`,
    {
      headers: { Host: "attacker.example" },
    },
  );
  expect(hosted.status()).toBe(403);
  await context.close();
});

test("status shows the connection, the version, the install and Balance of Power", async ({
  page,
  launcher,
}) => {
  await openPage(page, launcher.url);
  const region = page.getByRole("status");
  await expect(region).toContainText("Connected to the launcher.");
  await expect(region).toContainText("Launcher version: 0.1.0");
  await expect(region).toContainText(`Game install: ${launcher.installFolder}`);
  await expect(region).toContainText("Balance of Power: found.");
  await expect(page.getByRole("alert")).toBeHidden();
});

test("missions list each menu, with availability in words", async ({
  page,
  launcher,
}) => {
  await openPage(page, launcher.url);
  const missions = page.getByRole("region", { name: "Missions" });
  await expect(
    missions.getByRole("heading", { level: 3, name: "Training missions" }),
  ).toBeVisible();
  await expect(
    missions.getByRole("listitem").filter({ hasText: "First Flight" }),
  ).toHaveText("Basic: First Flight (alpha.tie), available Show First Flight");
  await expect(
    missions.getByRole("listitem").filter({ hasText: "Second Flight" }),
  ).toHaveText("Basic: Second Flight (bravo.tie), not available");
  await expect(
    missions.getByRole("listitem").filter({ hasText: "Open Field" }),
  ).toHaveText("Open: Open Field (delta.tie), available Show Open Field");
  await expect(missions.getByRole("heading", { level: 3 })).toHaveCount(6);
  await expect(missions).toContainText("was not found (tourn\\mission.lst)");
});

async function askFromSecondSocket(
  context: BrowserContext,
  origin: string,
  mission: { mission_type: string; id: number },
): Promise<unknown> {
  return callFromSecondSocket(context, origin, "page.show_mission", mission);
}

async function callFromSecondSocket(
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

test("a mission shown from a second socket is marked, scrolled to and announced", async ({
  page,
  launcher,
  context,
}) => {
  await page.setViewportSize({ width: 800, height: 260 });
  await openPage(page, launcher.url);
  const far = page.getByRole("listitem").filter({ hasText: "Open Field" });
  const first = page.getByRole("listitem").filter({ hasText: "First Flight" });
  await expect(page.locator("[aria-current]")).toHaveCount(0);
  await expect(far).not.toBeInViewport();
  await page.getByRole("link", { name: "Settings" }).focus();
  const reply = await askFromSecondSocket(context, launcher.origin, {
    mission_type: "melee",
    id: 7,
  });
  expect(reply).toEqual({ id: 1, ok: true, result: { shown: true } });
  await expect(far).toHaveAttribute("aria-current", "true");
  await expect(page.locator("[aria-current]")).toHaveCount(1);
  await expect(far).toBeInViewport();
  await expect(page.locator("#mission-shown")).toHaveText("Showing Open Field");
  await expect(page.locator("#mission-shown")).toHaveAttribute(
    "aria-live",
    "polite",
  );
  const mark = await far.evaluate(
    (el) => getComputedStyle(el, "::before").content,
  );
  expect(mark).toContain("\u25B6");
  await expect(page.getByRole("link", { name: "Settings" })).toBeFocused();
  await askFromSecondSocket(context, launcher.origin, {
    mission_type: "training",
    id: 1,
  });
  await expect(first).toHaveAttribute("aria-current", "true");
  await expect(far).not.toHaveAttribute("aria-current", "true");
  await expect(page.locator("#mission-shown")).toHaveText(
    "Showing First Flight",
  );
  const missing = await askFromSecondSocket(context, launcher.origin, {
    mission_type: "training",
    id: 99,
  });
  expect(missing).toEqual({ id: 1, ok: true, result: { shown: false } });
  await expect(first).toHaveAttribute("aria-current", "true");
  const found = await new AxeBuilder({ page }).withTags(AXE_TAGS).analyze();
  expect(found.violations).toEqual([]);
});

test("art scaling has three choices, one plain sentence each", async ({
  page,
  launcher,
}) => {
  await openPage(page, launcher.url);
  const group = page.getByRole("group", { name: "Art scaling" });
  await expect(group.getByRole("radio")).toHaveCount(3);
  await expect(group.getByLabel("Whole pixels")).toBeChecked();
  for (const name of ["Whole pixels", "Engine fit", "Sharp bilinear"]) {
    const radio = group.getByRole("radio", { name });
    await expect(radio).toHaveAccessibleDescription(/\.$/);
  }
});

test("a setting is saved, survives a reload and reaches a second page", async ({
  page,
  launcher,
  context,
}) => {
  await openPage(page, launcher.url);
  const second = await context.newPage();
  await second.goto(`${launcher.origin}/`);
  await expect(second.getByLabel("Whole pixels")).toBeEnabled();
  await page.getByText("Engine fit", { exact: true }).click();
  await expect(page.getByLabel("Engine fit")).toBeChecked();
  await expect(second.getByLabel("Engine fit")).toBeChecked();
  await expect
    .poll(() => launcher.readSettings())
    .toEqual({ art_scaling: "engine_fit" });
  await page.reload();
  await expect(page.getByLabel("Engine fit")).toBeChecked();
});

test("a keyboard alone reaches every control, each with a visible focus", async ({
  page,
  launcher,
}) => {
  await openPage(page, launcher.url);
  await page.locator("body").click({ position: { x: 1, y: 1 } });
  const seen: string[] = [];
  const focusRing = async (): Promise<string[]> =>
    page.evaluate(() => {
      const el = document.activeElement;
      if (el === null) return ["none", "0px"];
      const style = getComputedStyle(el);
      const name =
        el.getAttribute("aria-label") ??
        (el instanceof HTMLInputElement
          ? (el.labels?.[0]?.textContent ?? "")
          : el.textContent);
      return [name.trim(), style.outlineStyle, style.outlineWidth];
    });
  await page.evaluate(() => {
    if (document.activeElement instanceof HTMLElement)
      document.activeElement.blur();
  });
  for (let step = 0; step < 10; step += 1) {
    await page.keyboard.press("Tab");
    const [name = "", outline = "none", width = "0px"] = await focusRing();
    expect(outline, `${name} shows a focus outline`).not.toBe("none");
    expect(
      parseFloat(width),
      `${name} outline is 3px or more`,
    ).toBeGreaterThanOrEqual(3);
    seen.push(name);
  }
  expect(seen).toEqual([
    "Skip to the page content",
    "Status",
    "Missions",
    "Briefing",
    "Battle",
    "Game options",
    "Settings",
    "Show First Flight",
    "Show Open Field",
    "Whole pixels",
  ]);
  await page.keyboard.press("ArrowDown");
  await expect(page.getByLabel("Engine fit")).toBeChecked();
  await expect(page.getByLabel("Engine fit")).toBeFocused();
  await page.keyboard.press("ArrowDown");
  await expect(page.getByLabel("Sharp bilinear")).toBeChecked();
  await expect
    .poll(() => launcher.readSettings())
    .toEqual({ art_scaling: "sharp_bilinear" });
});

test("the skip link lands on the page content", async ({ page, launcher }) => {
  await openPage(page, launcher.url);
  const link = page.getByRole("link", { name: "Skip to the page content" });
  const hidden = await link.boundingBox();
  expect((hidden?.y ?? 0) + (hidden?.height ?? 0)).toBeLessThanOrEqual(0);
  await page.keyboard.press("Tab");
  await expect(link).toBeFocused();
  const shown = await link.boundingBox();
  expect(shown?.y ?? -1).toBeGreaterThanOrEqual(0);
  await page.keyboard.press("Enter");
  await expect(page.locator("main")).toBeFocused();
});

test("a dropped connection is said so and offers Reconnect", async ({
  page,
  launcher,
}) => {
  await openPage(page, launcher.url);
  await expect(page.getByRole("button", { name: "Reconnect" })).toBeHidden();
  await launcher.stop();
  await expect(
    page.getByText("The connection to the launcher has dropped."),
  ).toBeVisible();
  const button = page.getByRole("button", { name: "Reconnect" });
  await expect(button).toBeVisible();
  await expect(page.getByLabel("Whole pixels")).toBeDisabled();
  await button.focus();
  const attempt = page.waitForEvent("websocket");
  await page.keyboard.press("Enter");
  await attempt;
  await expect(
    page.getByText("The connection to the launcher has dropped."),
  ).toBeVisible();
});

for (const scheme of ["light", "dark"] as const) {
  test(`axe finds nothing in the ${scheme} scheme`, async ({
    page,
    launcher,
  }) => {
    await page.emulateMedia({ colorScheme: scheme });
    await openPage(page, launcher.url);
    const found = await new AxeBuilder({ page }).withTags(AXE_TAGS).analyze();
    expect(found.violations).toEqual([]);
    expect(found.passes.map((rule) => rule.id)).toContain("color-contrast");
  });

  test(`the ${scheme} scheme has a ${scheme} background`, async ({
    page,
    launcher,
  }) => {
    await page.emulateMedia({ colorScheme: scheme });
    await openPage(page, launcher.url);
    const brightness = await page.evaluate(() => {
      const color = getComputedStyle(document.body).backgroundColor;
      const [red = 0, green = 0, blue = 0] =
        color.match(/\d+/g)?.map(Number) ?? [];
      return (red + green + blue) / 3;
    });
    if (scheme === "dark") expect(brightness).toBeLessThan(60);
    else expect(brightness).toBeGreaterThan(200);
  });

  test(`axe finds nothing in the ${scheme} scheme once the connection drops`, async ({
    page,
    launcher,
  }) => {
    await page.emulateMedia({ colorScheme: scheme });
    await openPage(page, launcher.url);
    await launcher.stop();
    await expect(page.getByRole("button", { name: "Reconnect" })).toBeVisible();
    const found = await new AxeBuilder({ page }).withTags(AXE_TAGS).analyze();
    expect(found.violations).toEqual([]);
  });
}

test("the page keeps scripts and styles in their files", async ({
  page,
  launcher,
}) => {
  const problems: string[] = [];
  page.on("pageerror", (error) => problems.push(error.message));
  page.on("console", (message) => {
    if (message.type() === "error") problems.push(message.text());
  });
  await openPage(page, launcher.url);
  const counts = await page.evaluate(() => {
    const all = Array.from(document.querySelectorAll("*"));
    const handlers = all.filter((el) =>
      el.getAttributeNames().some((name) => name.startsWith("on")),
    );
    return {
      inlineStyles: document.querySelectorAll("[style]").length,
      inlineScripts: document.querySelectorAll("script:not([src])").length,
      handlers: handlers.length,
    };
  });
  expect(counts).toEqual({ inlineStyles: 0, inlineScripts: 0, handlers: 0 });
  expect(problems).toEqual([]);
});

async function smallControls(page: Page): Promise<string[]> {
  return page.evaluate(() => {
    const controls = document.querySelectorAll(
      "a, button:not([hidden]), input",
    );
    return Array.from(controls)
      .map((el) => ({
        name: el.id || el.textContent || "",
        box: el.getBoundingClientRect(),
      }))
      .filter(({ box }) => box.width > 0 && (box.width < 24 || box.height < 24))
      .map(({ name }) => name);
  });
}

test("controls are at least 24 by 24 pixels and the page reflows at 320 wide", async ({
  page,
  launcher,
}) => {
  await page.setViewportSize({ width: 320, height: 640 });
  await openPage(page, launcher.url);
  expect(await smallControls(page)).toEqual([]);
  await launcher.stop();
  await expect(page.getByRole("button", { name: "Reconnect" })).toBeVisible();
  expect(await smallControls(page)).toEqual([]);
  const overflow = await page.evaluate(
    () =>
      document.documentElement.scrollWidth -
      document.documentElement.clientWidth,
  );
  expect(overflow).toBeLessThanOrEqual(0);
});

test("smooth scrolling is only for people who have not asked for less motion", async ({
  page,
  launcher,
}) => {
  const behavior = (): Promise<string> =>
    page.evaluate(
      () => getComputedStyle(document.documentElement).scrollBehavior,
    );
  await page.emulateMedia({ reducedMotion: "reduce" });
  await openPage(page, launcher.url);
  expect(await behavior()).toBe("auto");
  await page.emulateMedia({ reducedMotion: "no-preference" });
  expect(await behavior()).toBe("smooth");
});

const SOLO = {
  player: "7001",
  mission_type: "training",
  side: "rebel",
  mission: "alpha.tie",
  pilot: "Ace",
};

async function startBattle(
  context: BrowserContext,
  origin: string,
  changes: Record<string, string> = {},
): Promise<Record<string, unknown>> {
  const reply = await callFromSecondSocket(context, origin, "launch.solo", {
    ...SOLO,
    ...changes,
  });
  return reply.result;
}

test("with no battle the page says so and offers no Cancel", async ({
  page,
  launcher,
}) => {
  await openPage(page, launcher.url);
  await expect(page.locator("#battle-note")).toHaveText(
    "No battle is flying. Battles start in Discord.",
  );
  await expect(
    page.getByRole("button", { name: "Cancel the battle" }),
  ).toBeHidden();
  await expect(page.locator("#battle-ending")).toHaveAttribute("hidden", "");
  await expect(page.locator("#options-list dt")).toHaveCount(7);
  await expect(page.locator("#options-list dd")).toHaveText(
    Array<string>(7).fill("As the game has it"),
  );
});

test("no control on the page starts a battle", async ({ page, launcher }) => {
  await openPage(page, launcher.url);
  const names = await page
    .getByRole("button")
    .evaluateAll((all) => all.map((b) => b.textContent.trim()));
  for (const name of names) expect(name).not.toMatch(/launch|fly|start|solo/i);
  await expect(
    page.locator(
      "#battle select, #battle input, #options input, #options select",
    ),
  ).toHaveCount(0);
});

test("a battle started from a second socket is shown with Cancel, and Cancel stops it", async ({
  page,
  launcher,
  context,
}) => {
  await openPage(page, launcher.url);
  const result = await startBattle(context, launcher.origin);
  expect(result.started).toBe(true);
  await expect(page.locator("#battle-note")).toHaveText("A battle is flying.");
  await expect(page.locator("#battle-line")).toHaveText(
    "First Flight, flown by Ace.",
  );
  const cancel = page.getByRole("button", { name: "Cancel the battle" });
  await expect(cancel).toBeVisible();
  await expect(cancel).toBeEnabled();
  await cancel.click();
  await expect(page.locator("#battle-ending")).toHaveText("cancelled");
  await expect(page.locator("#battle-note")).toHaveText("The battle is over.");
  await expect(cancel).toBeHidden();
  const again = await startBattle(context, launcher.origin);
  expect(again.started).toBe(true);
});

test("Cancel works from the keyboard and keeps focus in the battle section", async ({
  page,
  launcher,
  context,
}) => {
  await openPage(page, launcher.url);
  await startBattle(context, launcher.origin);
  const cancel = page.getByRole("button", { name: "Cancel the battle" });
  await expect(cancel).toBeVisible();
  await cancel.focus();
  await page.keyboard.press("Enter");
  await expect(page.locator("#battle-ending")).toHaveText("cancelled");
  await expect(page.locator("#battle-note")).toBeFocused();
  await expect(page.locator("#battle-note")).toHaveAttribute(
    "aria-live",
    "polite",
  );
});

test("a battle that ends by itself shows its ending line", async ({
  page,
  launcher,
  context,
}) => {
  await launcher.behave("done");
  await openPage(page, launcher.url);
  const result = await startBattle(context, launcher.origin);
  expect(result.started).toBe(true);
  await expect(page.locator("#battle-ending")).toHaveText(
    "flown: completed, score 10, kills 2, losses 0, 61 s",
  );
  await expect(
    page.getByRole("button", { name: "Cancel the battle" }),
  ).toBeHidden();
});

test("a page opened while a battle flies shows it and can cancel it", async ({
  page,
  launcher,
  context,
}) => {
  await openPage(page, launcher.url);
  await startBattle(context, launcher.origin);
  const late = await context.newPage();
  await late.goto(launcher.origin + "/");
  await expect(late.locator("#battle-line")).toHaveText(
    "First Flight, flown by Ace.",
  );
  await late.getByRole("button", { name: "Cancel the battle" }).click();
  await expect(late.locator("#battle-ending")).toHaveText("cancelled");
  await expect(page.locator("#battle-ending")).toHaveText("cancelled");
});

test("a second battle while one flies is refused in words and the first stays", async ({
  page,
  launcher,
  context,
}) => {
  await openPage(page, launcher.url);
  await startBattle(context, launcher.origin);
  const second = await startBattle(context, launcher.origin, { pilot: "Cee" });
  expect(second.started).toBe(false);
  expect(second.message).toContain("already flying");
  await expect(page.locator("#battle-line")).toHaveText(
    "First Flight, flown by Ace.",
  );
});

test("the game options are shown read-only and redrawn when they change", async ({
  page,
  launcher,
  context,
}) => {
  await openPage(page, launcher.url);
  const set = async (name: string, value: string | null) =>
    callFromSecondSocket(context, launcher.origin, "options.set", {
      player: "7001",
      name,
      value,
    });
  await set("difficulty", "hard");
  await set("mission_time_limit", "5");
  await expect(
    page.getByRole("region", { name: "Game options" }),
  ).toBeVisible();
  const terms = page.locator("#options-list dt");
  await expect(terms).toHaveCount(7);
  await expect(page.locator("#options-list dd").first()).toHaveText("Hard");
  await expect(
    page.locator("#options-list dt", { hasText: "Mission time limit" }),
  ).toBeVisible();
  await expect(page.locator("#options-list")).toContainText("5 minutes");
  await expect(page.locator("#options-list")).toContainText(
    "As the game has it",
  );
  await set("difficulty", null);
  await expect(page.locator("#options-list dd").first()).toHaveText(
    "As the game has it",
  );
  await expect(
    page.locator("#options input, #options select, #options button"),
  ).toHaveCount(0);
});

for (const scheme of ["light", "dark"] as const) {
  test(`axe finds nothing in the ${scheme} scheme with a battle flying and options set`, async ({
    page,
    launcher,
    context,
  }) => {
    await page.emulateMedia({ colorScheme: scheme });
    await openPage(page, launcher.url);
    await callFromSecondSocket(context, launcher.origin, "options.set", {
      player: "7001",
      name: "collisions",
      value: "off",
    });
    await startBattle(context, launcher.origin);
    await expect(page.locator("#battle-line")).toBeVisible();
    await expect(page.locator("#options-list")).toContainText("Off");
    const flying = await new AxeBuilder({ page }).withTags(AXE_TAGS).analyze();
    expect(flying.violations).toEqual([]);
    await page.getByRole("button", { name: "Cancel the battle" }).click();
    await expect(page.locator("#battle-ending")).toBeVisible();
    const ended = await new AxeBuilder({ page }).withTags(AXE_TAGS).analyze();
    expect(ended.violations).toEqual([]);
  });
}
