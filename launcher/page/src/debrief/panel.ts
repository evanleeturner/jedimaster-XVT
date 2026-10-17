/**
 * The Debriefing panel on the page: the kept results' picker, the two page
 * tabs, and the tables of the page on show.
 *
 * It draws a DebriefView and reports the player's picks through the two
 * callbacks; it changes no state of its own. The tabs follow the ARIA tabs
 * pattern: one tab stop, arrow keys, Home and End move between the tabs on
 * show (a tab the view hides is skipped).
 */
import type { DebriefView } from "../state.ts";
import type { DebriefTab, Table } from "./view.ts";

type Kind<T extends Element> = new (...args: never[]) => T;

function find<T extends Element>(kind: Kind<T>, selector: string): T {
  const found = document.querySelector(selector);
  if (!(found instanceof kind)) {
    throw new Error(`the page has no ${selector}`);
  }
  return found;
}

function isTab(value: string | undefined): value is DebriefTab {
  return value === "overview" || value === "statistics";
}

/** Return one table as the DOM draws it: a caption, a header row, row headers. */
export function tableElement(table: Table): HTMLTableElement {
  const element = document.createElement("table");
  element.createCaption().textContent = table.caption;
  const head = element.createTHead().insertRow();
  for (const column of table.columns) {
    const cell = document.createElement("th");
    cell.scope = "col";
    cell.textContent = column;
    head.append(cell);
  }
  const body = element.createTBody();
  for (const row of table.rows) {
    const line = body.insertRow();
    row.forEach((text, index) => {
      const cell = document.createElement(index === 0 ? "th" : "td");
      if (cell instanceof HTMLTableCellElement && index === 0) {
        cell.scope = "row";
      }
      cell.textContent = text;
      line.append(cell);
    });
  }
  return element;
}

export class DebriefPanel {
  private readonly note = find(HTMLParagraphElement, "#debrief-note");
  private readonly pickBox = find(HTMLDivElement, "#debrief-pick-box");
  private readonly pick = find(HTMLSelectElement, "#debrief-pick");
  private readonly title = find(HTMLParagraphElement, "#debrief-title");
  private readonly tabList = find(HTMLDivElement, "#debrief-tabs");
  private readonly page = find(HTMLDivElement, "#debrief-page");
  private readonly badge = find(HTMLSpanElement, "#debrief-badge");
  private readonly tabs = Array.from(
    this.tabList.querySelectorAll<HTMLButtonElement>("[role=tab]"),
  );
  private pickSignature = "";
  private pageSignature = "";

  constructor(
    onPick: (resultId: number) => void,
    onTab: (tab: DebriefTab) => void,
  ) {
    this.pick.addEventListener("change", () => {
      const id = Number(this.pick.value);
      if (Number.isInteger(id) && id > 0) onPick(id);
    });
    for (const tab of this.tabs) {
      tab.addEventListener("click", () => {
        if (isTab(tab.dataset.tab)) onTab(tab.dataset.tab);
      });
      tab.addEventListener("keydown", (event) => {
        const next = this.neighbour(tab, event.key);
        if (next === undefined) return;
        event.preventDefault();
        next.focus();
        if (isTab(next.dataset.tab)) onTab(next.dataset.tab);
      });
    }
  }

  /** Return the tab an arrow, Home or End key moves to from ``tab``. */
  private neighbour(
    tab: HTMLButtonElement,
    key: string,
  ): HTMLButtonElement | undefined {
    const tabs = this.tabs.filter((t) => !t.hidden);
    const at = tabs.indexOf(tab);
    const last = tabs.length - 1;
    if (key === "ArrowRight") return tabs[at === last ? 0 : at + 1];
    if (key === "ArrowLeft") return tabs[at === 0 ? last : at - 1];
    if (key === "Home") return tabs[0];
    if (key === "End") return tabs[last];
    return undefined;
  }

  render(view: DebriefView): void {
    this.badge.hidden = !view.fresh;
    this.note.hidden = view.note === null;
    this.note.textContent = view.note ?? "";
    this.pickBox.hidden = view.options.length === 0;
    const pickSignature = JSON.stringify(view.options);
    if (pickSignature !== this.pickSignature) {
      this.pickSignature = pickSignature;
      this.pick.replaceChildren(
        ...view.options.map((option) => {
          const item = document.createElement("option");
          item.value = String(option.id);
          item.textContent = option.label;
          item.selected = option.current;
          return item;
        }),
      );
      if (!view.options.some((o) => o.current)) this.pick.selectedIndex = -1;
    }
    const shown = view.heading !== null;
    this.title.hidden = !shown;
    this.title.textContent = view.heading ?? "";
    this.tabList.hidden = !shown;
    this.page.hidden = !shown;
    for (const tab of this.tabs) {
      tab.hidden =
        isTab(tab.dataset.tab) && view.hiddenTabs.includes(tab.dataset.tab);
      const selected = tab.dataset.tab === view.tab;
      tab.setAttribute("aria-selected", String(selected));
      tab.tabIndex = selected ? 0 : -1;
      if (selected) this.page.setAttribute("aria-labelledby", tab.id);
    }
    const pageSignature = JSON.stringify(view.tables);
    if (pageSignature !== this.pageSignature) {
      this.pageSignature = pageSignature;
      this.page.replaceChildren(...view.tables.map(tableElement));
    }
  }
}
