import { BriefingPanel } from "./briefing/panel.ts";
import type { Request } from "./codec.ts";
import { decode, encodeRequest, isArtScaling, isMissionType } from "./codec.ts";
import type { BriefingBundle } from "./generated/briefing.ts";
import { logger } from "./logger.ts";
import type { Action, MenuView, State, View } from "./state.ts";
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
const shownLine = find(HTMLParagraphElement, "#mission-shown");
const scalingGroup = find(HTMLFieldSetElement, "#art-scaling");

let state: State = initialState();
const briefingPanel = new BriefingPanel();
let shownBundle: BriefingBundle | null = null;
let menuSignature = "";
let socket: WebSocket | null = null;
let nextId = 1;
let scrolledFor = 0;

function setText(element: HTMLElement, text: string): void {
  if (element.textContent !== text) element.textContent = text;
}

function menuElements(menu: MenuView): HTMLElement[] {
  const heading = document.createElement("h3");
  heading.textContent = menu.heading;
  const parts: HTMLElement[] = [heading];
  if (menu.note !== null) {
    const note = document.createElement("p");
    note.textContent = menu.note;
    parts.push(note);
  }
  if (menu.entries.length > 0) {
    const list = document.createElement("ul");
    list.className = "mission-list";
    for (const entry of menu.entries) {
      const item = document.createElement("li");
      item.className = "mission-item";
      item.append(entry.text);
      if (entry.current) item.setAttribute("aria-current", "true");
      if (entry.pick !== null) {
        const pick = document.createElement("button");
        pick.type = "button";
        pick.className = "button pick-button";
        pick.textContent = entry.pick.label;
        pick.dataset.missionType = entry.pick.missionType;
        pick.dataset.missionId = String(entry.pick.id);
        item.append(" ", pick);
      }
      list.append(item);
    }
    parts.push(list);
  }
  return parts;
}

function renderMenus(view: View): void {
  const signature = JSON.stringify(
    view.menus.map((m) => [
      m.heading,
      m.note,
      m.entries.map((e) => [e.text, e.pick]),
    ]),
  );
  if (signature !== menuSignature) {
    menuSignature = signature;
    missionsBody.replaceChildren(...view.menus.flatMap(menuElements));
  }
  const items = missionsBody.querySelectorAll("li.mission-item");
  const entries = view.menus.flatMap((m) => m.entries);
  items.forEach((item, i) => {
    if (entries[i]?.current === true) item.setAttribute("aria-current", "true");
    else item.removeAttribute("aria-current");
  });
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
  renderMenus(view);
  renderBriefing(view);
  setText(shownLine, view.shownText);
  const mark = missionsBody.querySelector('[aria-current="true"]');
  if (mark !== null && view.shownCount !== scrolledFor) {
    scrolledFor = view.shownCount;
    mark.scrollIntoView({ block: "center" });
  }
  if (view.artScaling !== null) briefingPanel.setScaling(view.artScaling);
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
      args: { mission_type: message.data.mission_type, id: message.data.id },
    });
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
    send({ command: "settings.get", args: {} });
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
  }
}

scalingGroup.addEventListener("change", onScalingChange);
missionsBody.addEventListener("click", onMissionsClick);
reconnectButton.addEventListener("click", connect);
render(describe(state));
connect();
