import { BriefingPanel } from "./briefing/panel.ts";
import type { Request } from "./codec.ts";
import { decode, encodeRequest, isArtScaling, isMissionType } from "./codec.ts";
import { screenTabs } from "./debrief/look.ts";
import { DebriefPanel } from "./debrief/panel.ts";
import { DebriefScreen } from "./debrief/screen.ts";
import { tablesOf } from "./debrief/view.ts";
import type { SideButtonId, SideView } from "./frame/side.ts";
import {
  keyOfShown,
  missionArgs,
  press,
  SIDE_BUTTONS,
  SIDE_PATH,
} from "./frame/side.ts";
import { FrameSkin } from "./frame/skin.ts";
import type { BriefingBundle } from "./generated/briefing.ts";
import { logger } from "./logger.ts";
import type { Action, KindView, State, View } from "./state.ts";
import { describe, initialState, PAGE_VERSION, reduce } from "./state.ts";

type Kind<T extends Element> = abstract new (...args: never[]) => T;

function find<T extends Element>(kind: Kind<T>, selector: string): T {
  const found = document.querySelector(selector);
  if (!(found instanceof kind)) {
    throw new Error(`invariant: ${selector} is missing from the page`);
  }
  return found;
}

const connectionLine = find(HTMLParagraphElement, "#status-connection");
const versionLine = find(HTMLParagraphElement, "#status-version");
const installLine = find(HTMLParagraphElement, "#status-install");
const balanceLine = find(HTMLParagraphElement, "#status-balance");
const problemLine = find(HTMLParagraphElement, "#problem");
const reconnectButton = find(HTMLButtonElement, "#reconnect");
const missionsNote = find(HTMLParagraphElement, "#missions-note");
const missionsBody = find(HTMLDivElement, "#missions-body");
const kindsList = find(HTMLUListElement, "#missions-kinds");
const groupsList = find(HTMLUListElement, "#missions-groups");
const missionList = find(HTMLUListElement, "#missions-list");
const groupHeading = find(HTMLHeadingElement, "#missions-group-heading");
const kindNote = find(HTMLParagraphElement, "#missions-kind-note");
const shownLine = find(HTMLParagraphElement, "#mission-shown");
const shownHeading = find(HTMLDivElement, "#shown-heading");
const shownWhereLine = find(HTMLParagraphElement, "#shown-where");
const shownTitle = find(HTMLHeadingElement, "#shown-title");
const emptyHint = find(HTMLParagraphElement, "#empty-hint");
const openMissionsButton = find(HTMLButtonElement, "#open-missions");
const scalingGroup = find(HTMLFieldSetElement, "#art-scaling");
const battleNote = find(HTMLParagraphElement, "#battle-note");
const battleBadge = find(HTMLSpanElement, "#battle-badge");
const battleStrip = find(HTMLDivElement, "#battle-strip");
const stripRow = find(HTMLDivElement, "#strip-row");
const stripTag = find(HTMLSpanElement, "#strip-tag");
const battleLine = find(HTMLSpanElement, "#battle-line");
const battleEnding = find(HTMLSpanElement, "#battle-ending");
const cancelButton = find(HTMLButtonElement, "#battle-cancel");
const optionsList = find(HTMLElement, "#options-list");
const optionsCopy = find(HTMLElement, "#options-copy-grid");
const barVersion = find(HTMLParagraphElement, "#bar-version");
const pilotLine = find(HTMLParagraphElement, "#pilot-line");
const barPilot = find(HTMLParagraphElement, "#bar-pilot");
const sideSprites = find(HTMLDivElement, "#side-sprites");
const sideNote = find(HTMLParagraphElement, "#side-note");
const sideButtons = new Map(
  SIDE_BUTTONS.map(
    (button) =>
      [
        button.id,
        find(HTMLButtonElement, `.side-btn[data-side="${button.id}"]`),
      ] as const,
  ),
);
const menuButtons = Array.from(
  document.querySelectorAll<HTMLButtonElement>(".menu-btn[aria-controls]"),
);

let state: State = initialState();
const briefingPanel = new BriefingPanel();
const debriefPanel = new DebriefPanel(
  (resultId) => {
    send({ command: "results.get", args: { result_id: resultId } });
  },
  (tab) => {
    dispatch({ kind: "debrief-tab", tab });
  },
);
const debriefScreen = new DebriefScreen(() => {
  closePanel(true);
});
let framed = false;
const frameSkin = new FrameSkin(
  find(HTMLCanvasElement, "#frame-art"),
  (layout) => {
    briefingPanel.setFrame(layout);
    framed = layout !== null;
    debriefScreen.setLayout(layout);
    if (lastView !== null) renderDebrief(lastView);
    if (lastView !== null) renderSide(lastView.side, lastView.sideNote);
  },
);
let shownBundle: BriefingBundle | null = null;
let openPanel = "";
let pickedKind = "";
let pickedGroup = "";
let syncedFor = 0;
let lastView: View | null = null;
let kindSignature = "";
let groupSignature = "";
let listSignature = "";
let socket: WebSocket | null = null;
let nextId = 1;

function setText(element: HTMLElement, text: string): void {
  if (element.textContent !== text) element.textContent = text;
}

function pickButton(label: string, count: number | null): HTMLElement {
  const item = document.createElement("li");
  const button = document.createElement("button");
  button.type = "button";
  button.className = "row-btn";
  const name = document.createElement("span");
  name.textContent = label;
  button.append(name);
  if (count !== null) {
    const number = document.createElement("span");
    number.className = "row-count";
    number.textContent = String(count);
    button.append(number);
  }
  item.append(button);
  return item;
}

function markPressed(list: HTMLElement, label: string): void {
  for (const button of list.querySelectorAll("button")) {
    const name = button.firstElementChild?.textContent;
    button.setAttribute("aria-pressed", String(name === label));
  }
}

function missionItem(
  row: KindView["groups"][number]["missions"][number],
): HTMLElement {
  const item = document.createElement("li");
  item.className = "mission-item";
  const title = document.createElement("span");
  title.className = "mission-title";
  title.textContent = row.title;
  item.append(title);
  if (row.pick !== null) {
    const pick = document.createElement("button");
    pick.type = "button";
    pick.className = "button pick-button";
    pick.textContent = "Show";
    pick.setAttribute("aria-label", row.pick.label);
    pick.dataset.missionType = row.pick.missionType;
    pick.dataset.missionId = String(row.pick.id);
    const now = document.createElement("span");
    now.className = "mission-now";
    now.textContent = "Showing";
    item.append(pick, now);
  } else {
    const locked = document.createElement("span");
    locked.className = "mission-locked";
    locked.textContent = "Not open yet";
    item.append(locked);
  }
  return item;
}

function renderMissions(view: View): void {
  const kinds = view.kinds;
  if (!kinds.some((k) => k.id === pickedKind)) pickedKind = kinds[0]?.id ?? "";
  const kind = kinds.find((k) => k.id === pickedKind);
  if (!kind?.groups.some((g) => g.name === pickedGroup)) {
    pickedGroup = kind?.groups[0]?.name ?? "";
  }
  const kindKey = JSON.stringify(kinds.map((k) => [k.id, k.label, k.count]));
  if (kindKey !== kindSignature) {
    kindSignature = kindKey;
    kindsList.replaceChildren(
      ...kinds.map((k) => {
        const item = pickButton(k.label, k.count);
        item.querySelector("button")?.setAttribute("data-kind", k.id);
        return item;
      }),
    );
  }
  for (const button of kindsList.querySelectorAll("button")) {
    button.setAttribute(
      "aria-pressed",
      String(button.dataset.kind === pickedKind),
    );
  }
  const groups = kind?.groups ?? [];
  const groupKey = JSON.stringify([
    pickedKind,
    groups.map((g) => [g.name, g.missions.length]),
  ]);
  if (groupKey !== groupSignature) {
    groupSignature = groupKey;
    groupsList.replaceChildren(
      ...groups.map((g) => pickButton(g.name, g.missions.length)),
    );
  }
  markPressed(groupsList, pickedGroup);
  setText(groupHeading, pickedGroup === "" ? "Mission" : pickedGroup);
  kindNote.hidden = kind?.note == null;
  setText(kindNote, kind?.note ?? "");
  const rows = groups.find((g) => g.name === pickedGroup)?.missions ?? [];
  const listKey = JSON.stringify([
    pickedKind,
    pickedGroup,
    rows.map((r) => [r.title, r.pick]),
  ]);
  if (listKey !== listSignature) {
    listSignature = listKey;
    missionList.replaceChildren(...rows.map(missionItem));
  }
  Array.from(missionList.children).forEach((item, i) => {
    const current = rows[i]?.current === true;
    if (current) item.setAttribute("aria-current", "true");
    else item.removeAttribute("aria-current");
    const pick = item.querySelector("button");
    const now = item.querySelector(".mission-now");
    if (pick !== null) pick.hidden = current;
    if (now instanceof HTMLElement) now.hidden = !current;
  });
}

function followShown(view: View): void {
  if (view.shownCount === syncedFor) return;
  syncedFor = view.shownCount;
  const where = view.shownWhere;
  if (where === null) return;
  pickedKind = view.kinds.find((k) => k.label === where.kind)?.id ?? pickedKind;
  pickedGroup = where.group;
}

function renderShown(view: View): void {
  const where = view.shownWhere;
  shownHeading.hidden = where === null;
  setText(
    shownWhereLine,
    where === null ? "" : `${where.kind} \u00b7 ${where.group}`,
  );
  setText(shownTitle, where?.title ?? "");
  const waiting = view.briefing.status === "none";
  emptyHint.hidden = !waiting;
  openMissionsButton.hidden = !waiting;
}

function scrollToMark(): void {
  const mark = missionList.querySelector('[aria-current="true"]');
  if (mark instanceof HTMLElement) {
    missionList.scrollTop = mark.offsetTop - missionList.clientHeight / 2;
  }
}

function setPanel(id: string): void {
  openPanel = id;
  for (const button of menuButtons) {
    const panel = document.getElementById(
      button.getAttribute("aria-controls") ?? "",
    );
    const open = panel?.id === id;
    button.setAttribute("aria-expanded", String(open));
    if (panel !== null) panel.hidden = !open;
  }
  if (id === "panel-missions") scrollToMark();
  if (id === "panel-debrief" && state.debrief.fresh) {
    dispatch({ kind: "debrief-seen" });
  }
  if (lastView !== null) renderDebrief(lastView);
}

/** Draw the Debriefing panel: the 1997 screen when the page is framed and the menu is open. */
function renderDebrief(view: View): void {
  const shown = view.debrief.shown;
  const tab = debriefScreen.update({
    debrief: shown,
    tab: view.debrief.tab,
    chosen: view.sideFaction,
    open: openPanel === "panel-debrief",
  });
  if (tab === null || shown === null) {
    debriefPanel.render(view.debrief);
    return;
  }
  const offered = screenTabs(shown);
  debriefPanel.render({
    ...view.debrief,
    tab,
    tables: tablesOf(shown, tab),
    hiddenTabs: (["overview", "statistics"] as const).filter(
      (t) => !offered.includes(t),
    ),
  });
}

function openerOf(id: string): HTMLButtonElement | undefined {
  return menuButtons.find((b) => b.getAttribute("aria-controls") === id);
}

/** Close the open panel; with `refocus`, put focus back on its button. */
function closePanel(refocus: boolean): void {
  const opener = openerOf(openPanel);
  setPanel("");
  if (refocus) opener?.focus();
}

function onMenuButton(event: Event): void {
  const button = event.currentTarget;
  if (!(button instanceof HTMLButtonElement)) return;
  const id = button.getAttribute("aria-controls") ?? "";
  setPanel(openPanel === id ? "" : id);
}

function onOpenMissions(): void {
  setPanel("panel-missions");
  openerOf("panel-missions")?.focus();
}

function onKeydown(event: KeyboardEvent): void {
  if (event.key === "Escape" && openPanel !== "") closePanel(true);
}

function onDocumentClick(event: MouseEvent): void {
  const target = event.target;
  if (openPanel === "" || !(target instanceof Element)) return;
  if (target.closest(".dd, #open-missions") === null) closePanel(false);
}

function onKindsClick(event: Event): void {
  const target = event.target;
  if (!(target instanceof Element)) return;
  const button = target.closest("button");
  if (button === null) return;
  if (kindsList.contains(button) && button.dataset.kind !== undefined) {
    pickedKind = button.dataset.kind;
    pickedGroup = "";
  } else if (groupsList.contains(button)) {
    pickedGroup = button.firstElementChild?.textContent ?? "";
  } else return;
  if (lastView !== null) renderMissions(lastView);
}

function renderBattle(view: View): void {
  setText(battleNote, view.battleNote);
  const strip = view.battleLine !== null;
  battleStrip.classList.toggle("is-idle", !strip);
  stripRow.hidden = !strip;
  battleBadge.hidden = !view.cancelShown;
  setText(
    stripTag,
    view.battleEnding === null ? "Battle flying" : "Battle over",
  );
  battleLine.hidden = view.battleLine === null;
  setText(battleLine, view.battleLine ?? "");
  battleEnding.hidden = view.battleEnding === null;
  setText(battleEnding, view.battleEnding ?? "");
  cancelButton.hidden = !view.cancelShown;
  cancelButton.disabled = !view.cancelEnabled;
}

function renderOptions(view: View): void {
  const rows = view.optionsRows ?? [];
  const signature = JSON.stringify(rows);
  if (optionsList.dataset.signature === signature) return;
  optionsList.dataset.signature = signature;
  optionsList.replaceChildren(
    ...rows.flatMap((row) => {
      const term = document.createElement("dt");
      term.textContent = row.label;
      const value = document.createElement("dd");
      value.textContent = row.value;
      return [term, value];
    }),
  );
  optionsCopy.replaceChildren(
    ...rows.map((row) => {
      const item = document.createElement("span");
      item.className = "options-copy-item";
      if (row.name === "mission_time_limit") item.classList.add("is-wide");
      const label = document.createElement("span");
      label.className = "options-copy-label";
      label.textContent = `${row.label}:`;
      const value = document.createElement("span");
      value.className = "options-copy-value";
      value.textContent = row.value;
      item.append(label, value);
      return item;
    }),
  );
}

function renderBriefing(view: View): void {
  if (view.briefing.status !== "ready") {
    shownBundle = null;
    briefingPanel.hide(view.briefingNote ?? "");
    return;
  }
  if (view.briefing.bundle !== shownBundle) {
    shownBundle = view.briefing.bundle;
    void briefingPanel.show(view.briefing.bundle);
  }
}

const sideImages = new Map<string, HTMLImageElement>();

function sideImage(name: string): HTMLImageElement {
  const kept = sideImages.get(name);
  if (kept !== undefined) return kept;
  const image = new Image();
  image.alt = "";
  image.addEventListener("error", () => {
    logger.warn("a side button picture did not load", name);
    image.dataset.broken = "true";
    image.hidden = true;
  });
  image.src = `${SIDE_PATH}${name}.png`;
  sideSprites.append(image);
  sideImages.set(name, image);
  return image;
}

function renderSide(view: SideView, note: string | null): void {
  const wanted = new Set([
    ...view.buttons.map((b) => b.sprite),
    ...view.lights,
  ]);
  // The pictures are asked for only when the page wears the frame.
  if (framed) for (const name of wanted) sideImage(name);
  for (const [name, image] of sideImages) {
    image.hidden = !wanted.has(name) || image.dataset.broken !== undefined;
  }
  for (const button of view.buttons) {
    const element = sideButtons.get(button.id);
    if (element === undefined) continue;
    element.setAttribute("aria-pressed", String(button.selected));
    if (button.stepping) {
      element.setAttribute("aria-describedby", "side-step-hint");
    } else element.removeAttribute("aria-describedby");
  }
  setText(sideNote, note ?? "");
}

function render(view: View): void {
  setText(connectionLine, view.connectionText);
  setText(versionLine, view.versionText);
  setText(installLine, view.installText);
  setText(balanceLine, view.balanceText);
  reconnectButton.hidden = !view.showReconnect;
  problemLine.hidden = view.problem === null;
  setText(problemLine, view.problem ?? "");
  missionsNote.hidden = view.missionsNote === null;
  setText(missionsNote, view.missionsNote ?? "");
  lastView = view;
  followShown(view);
  renderMissions(view);
  renderShown(view);
  renderBriefing(view);
  renderBattle(view);
  renderOptions(view);
  renderDebrief(view);
  setText(pilotLine, view.pilotLine);
  setText(barPilot, view.pilotBar);
  setText(shownLine, view.shownText);
  setText(barVersion, view.launcherVersion ?? "");
  renderSide(view.side, view.sideNote);
  frameSkin.setMission(view.sideKind);
  frameSkin.setSide(view.sideFaction);
  if (view.artScaling !== null) {
    briefingPanel.setScaling(view.artScaling);
    frameSkin.setScaling(view.artScaling);
  }
  scalingGroup.disabled = !view.settingsEnabled;
  for (const input of scalingGroup.querySelectorAll("input")) {
    input.checked = input.value === view.artScaling;
  }
}

function dispatch(action: Action): void {
  logger.debug("action", action);
  state = reduce(state, action);
  render(describe(state));
}

type Body<R> = R extends { id: number } ? Omit<R, "id"> : never;

function send(request: Body<Request>): void {
  if (socket?.readyState !== WebSocket.OPEN) {
    logger.warn("not sent, the socket is not open", request.command);
    return;
  }
  const withId: Request = { id: nextId, ...request };
  nextId += 1;
  logger.debug("send", withId);
  socket.send(encodeRequest(withId));
}

function onMessage(event: MessageEvent): void {
  if (typeof event.data !== "string") {
    logger.warn("ignored a message that is not text");
    return;
  }
  const decoded = decode(event.data);
  if (!decoded.ok) {
    logger.warn("ignored a message:", decoded.reason);
    return;
  }
  logger.debug("receive", decoded.message);
  dispatch({ kind: "message", message: decoded.message });
  const { message } = decoded;
  if ("event" in message && message.event === "page.show_mission") {
    send({
      command: "briefing.get",
      args: missionArgs(message.data.mission_type, keyOfShown(message.data)),
    });
  }
  // A page that connects with results kept shows the newest one's debriefing.
  if ("ok" in message && message.ok && "results" in message.result) {
    const newest = message.result.results[0];
    if (newest !== undefined && state.debrief.shown === null) {
      send({ command: "results.get", args: { result_id: newest.result_id } });
    }
  }
}

function socketUrl(): URL {
  const url = new URL("/ws", window.location.href);
  url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
  return url;
}

function connect(): void {
  dispatch({ kind: "connecting" });
  const opened = new WebSocket(socketUrl());
  socket = opened;
  opened.addEventListener("open", () => {
    dispatch({ kind: "opened" });
    send({ command: "hello", args: { page_version: PAGE_VERSION } });
    send({ command: "install.status", args: {} });
    send({ command: "missions.list", args: {} });
    for (const mission_type of ["training", "combat", "campaign"] as const) {
      for (const side of ["rebel", "imperial"] as const) {
        send({ command: "launch.menu", args: { mission_type, side } });
      }
    }
    send({ command: "settings.get", args: {} });
    send({ command: "options.get", args: { player: null } });
    send({ command: "results.list", args: {} });
  });
  opened.addEventListener("message", onMessage);
  opened.addEventListener("close", () => {
    logger.info("the socket closed");
    dispatch({ kind: "closed" });
  });
}

function onScalingChange(event: Event): void {
  const input = event.target;
  if (input instanceof HTMLInputElement && isArtScaling(input.value)) {
    send({
      command: "settings.set",
      args: { name: "art_scaling", value: input.value },
    });
  }
}

function onMissionsClick(event: Event): void {
  const target = event.target;
  if (
    !(target instanceof HTMLButtonElement) ||
    target.dataset.missionType === undefined
  )
    return;
  const type = target.dataset.missionType;
  const id = Number(target.dataset.missionId);
  if (isMissionType(type) && Number.isInteger(id)) {
    send({ command: "page.show_mission", args: { mission_type: type, id } });
    closePanel(true);
  }
}

function onCancel(): void {
  const launch = state.launch;
  if (launch?.ending !== null) return;
  dispatch({ kind: "cancelling" });
  send({
    command: "launch.cancel",
    args: { launch_id: launch.started.launch_id },
  });
  battleStrip.focus();
}

function onSideButton(id: SideButtonId, backward: boolean): void {
  if (socket?.readyState !== WebSocket.OPEN) {
    logger.warn("side button ignored, the socket is not open", id);
    return;
  }
  const result = press(
    state.side,
    { menus: state.menus, solo: state.soloLists },
    id,
    backward,
  );
  logger.debug("side button", {
    button: id,
    backward,
    kind: result.model.kind,
    side: result.model.side,
    mission:
      result.show === null
        ? null
        : missionArgs(result.show.missionType, result.show.key),
  });
  dispatch({ kind: "side-pressed", press: result });
  if (result.show !== null) {
    send({
      command: "page.show_mission",
      args: missionArgs(result.show.missionType, result.show.key),
    });
  }
}

for (const [id, element] of sideButtons) {
  element.addEventListener("click", (event) => {
    onSideButton(id, event.shiftKey);
  });
  element.addEventListener("contextmenu", (event) => {
    event.preventDefault();
    onSideButton(id, true);
  });
}
cancelButton.addEventListener("click", onCancel);
scalingGroup.addEventListener("change", onScalingChange);
missionsBody.addEventListener("click", onMissionsClick);
missionsBody.addEventListener("click", onKindsClick);
for (const button of menuButtons)
  button.addEventListener("click", onMenuButton);
openMissionsButton.addEventListener("click", onOpenMissions);
document.addEventListener("keydown", onKeydown);
document.addEventListener("click", onDocumentClick);
reconnectButton.addEventListener("click", connect);
render(describe(state));
connect();
