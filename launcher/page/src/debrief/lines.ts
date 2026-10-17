/**
 * The lines the 1997 debriefing draws, worked out from the view model.
 *
 * Every drawn number is a cell of the tables `view.ts` makes (or a field of
 * the standing rows those tables are made from), put at the place and in the
 * colour `mission_debrief.c` gave it. A line has no pixels: the painter draws
 * it in the game's font. `source` names the cell a line shows, so a test can
 * hold each drawn number against its table.
 */
import type { Debrief } from "../generated/control.ts";
import type { Rect } from "./look.ts";
import { PLACES } from "./look.ts";
import type { DebriefTab, Table } from "./view.ts";
import {
  DEBRIEF_TABS,
  overviewTables,
  standingRows,
  statisticsTables,
} from "./view.ts";

/** The colour bytes a string may hold: 1 is the line's own colour. */
export const DEFAULT = "\u0001";
export const GREEN = "\u0002";
export const YELLOW_CODE = "\u0004";
export const LIGHT_BLUE = "\u0006";

export type Ink = "white" | "yellow" | "gray" | "red";

/** Where a line's text comes from: a cell of a table, or a whole row (column -1). */
export interface Source {
  readonly table: number;
  readonly row: number;
  readonly column: number;
}

export interface DrawnLine {
  readonly font: 12 | 15;
  readonly x: number;
  readonly y: number;
  readonly ink: Ink;
  readonly text: string;
  /** Draw centred in this rectangle instead of from `x`, `y`. */
  readonly centred?: Rect;
  /** Cut the text at this x. */
  readonly clipRight?: number;
  readonly source?: Source;
}

export const ROW = 15;
export const OVERVIEW = {
  headingY: 114,
  firstY: 134,
  teamX: 88,
  playerX: 103,
  playerRight: 225,
  scoreX: 228,
  killsX: 298,
  deathsX: 363,
  killedValueX: 208,
  killedByX: 259,
  killedByValueX: 379,
  lastY: 436,
} as const;
export const STATISTICS = {
  firstY: 111,
  rows: 21,
  labelX: 88,
  valueX: 268,
} as const;
/** The plate laid under a page's text: the text zone of both pages. */
export const TEXT_PLATE: Rect = { left: 82, top: 88, right: 438, bottom: 440 };
export const PILOT_PLATE: Rect = {
  left: 198,
  top: 450,
  right: 438,
  bottom: 466,
};

const LAST_COLOR_BYTE = 6;

/** Return `text` without its colour bytes: what a reader sees. */
export function plain(text: string): string {
  return Array.from(text)
    .filter((c) => c.charCodeAt(0) > LAST_COLOR_BYTE)
    .join("");
}

function titleLine(tab: DebriefTab): DrawnLine {
  const label = DEBRIEF_TABS.find((t) => t.id === tab)?.label ?? "";
  return {
    font: 15,
    x: PLACES.title.left,
    y: PLACES.title.top,
    ink: "white",
    text: label,
    centred: PLACES.title,
  };
}

/** Return the mission title the top bar shows: the text up to its last "(". */
export function missionTitle(title: string): string {
  const at = title.lastIndexOf("(");
  return at > 0 ? title.slice(0, at).trimEnd() : title;
}

/** Return the pilot line: the local rank in light blue, then the pilot's name. */
export function pilotLine(d: Debrief): DrawnLine {
  const local = d.players.find((p) => p.entry === d.local_entry);
  const rank =
    local === undefined ? "" : `${LIGHT_BLUE}${local.rank} ${DEFAULT}`;
  return {
    font: 12,
    x: PLACES.pilot.left,
    y: PLACES.pilot.top,
    ink: "yellow",
    text: `${rank}${d.pilot}`,
    centred: PLACES.pilot,
  };
}

/** Return the mission title line of the top bar. */
export function missionLine(d: Debrief): DrawnLine {
  return {
    font: 12,
    x: PLACES.mission.left,
    y: PLACES.mission.top,
    ink: "white",
    text: missionTitle(d.title),
    centred: PLACES.mission,
  };
}

function overviewStandings(d: Debrief, table: Table): DrawnLine[] {
  const lines: DrawnLine[] = [];
  const heading = (text: string, x: number): DrawnLine => ({
    font: 12,
    x,
    y: OVERVIEW.headingY,
    ink: "yellow",
    text,
  });
  lines.push(
    heading("Score", OVERVIEW.scoreX),
    heading("Kills", OVERVIEW.killsX),
    heading("Deaths", OVERVIEW.deathsX),
  );
  const rows = standingRows(d);
  rows.forEach((row, index) => {
    const y = OVERVIEW.firstY + ROW * index;
    const cells = table.rows[index] ?? [];
    const team = row.kind === "team";
    const name = team
      ? `${row.place <= 3 ? GREEN : DEFAULT}${String(row.place)}. ${DEFAULT}${row.name}`
      : row.left
        ? `[${row.rank === "" ? "" : `${row.rank} `}${row.name}]`
        : `${row.rank === "" ? "" : `${LIGHT_BLUE}${row.rank} ${DEFAULT}`}${row.name}`;
    lines.push({
      font: 12,
      x: team ? OVERVIEW.teamX : OVERVIEW.playerX,
      y,
      ink: !team && row.left ? "gray" : team ? "white" : "yellow",
      text: name,
      clipRight: team ? undefined : OVERVIEW.playerRight,
      source: { table: 0, row: index, column: team ? 0 : 1 },
    });
    const numbers: [number, number][] = [
      [3, OVERVIEW.scoreX],
      [4, OVERVIEW.killsX],
      [5, OVERVIEW.deathsX],
    ];
    for (const [column, x] of numbers) {
      lines.push({
        font: 12,
        x,
        y,
        ink: "white",
        text: cells[column] ?? "",
        source: { table: 0, row: index, column },
      });
    }
  });
  return lines;
}

function killLines(
  table: Table,
  tableIndex: number,
  top: number,
  labelX: number,
  valueX: number,
): DrawnLine[] {
  const lines: DrawnLine[] = [
    {
      font: 12,
      x: labelX,
      y: top,
      ink: "yellow",
      text: table.caption,
    },
  ];
  table.rows.forEach((cells, index) => {
    const y = top + ROW * (index + 1);
    if (y > OVERVIEW.lastY) return;
    lines.push(
      {
        font: 12,
        x: labelX,
        y,
        ink: "yellow",
        text: cells[0] ?? "",
        clipRight: valueX - 2,
        source: { table: tableIndex, row: index, column: 0 },
      },
      {
        font: 12,
        x: valueX,
        y,
        ink: "white",
        text: cells[1] ?? "",
        source: { table: tableIndex, row: index, column: 1 },
      },
    );
  });
  return lines;
}

/** Return the mission overview page's lines: title, standings, killed lists. */
export function overviewLines(d: Debrief): DrawnLine[] {
  const tables = overviewTables(d);
  const standings = tables[0];
  if (standings === undefined) return [titleLine("overview")];
  const lines = [titleLine("overview"), ...overviewStandings(d, standings)];
  const top = OVERVIEW.firstY + ROW * standings.rows.length + ROW;
  const [killed, killedBy] = [tables[1], tables[2]];
  if (killed !== undefined) {
    lines.push(
      ...killLines(killed, 1, top, OVERVIEW.teamX, OVERVIEW.killedValueX),
    );
  }
  if (killedBy !== undefined) {
    lines.push(
      ...killLines(
        killedBy,
        2,
        top,
        OVERVIEW.killedByX,
        OVERVIEW.killedByValueX,
      ),
    );
  }
  return lines;
}

/** One row of the statistics list, before it is placed. */
export type StatRow =
  | { readonly kind: "blank" }
  | { readonly kind: "heading"; readonly text: string }
  | {
      readonly kind: "line";
      readonly text: string;
      readonly source: Source;
    }
  | {
      readonly kind: "pair";
      readonly label: string;
      readonly value: string;
      readonly red: boolean;
      readonly table: number;
      readonly row: number;
    };

const TOTAL_FIRST = "Total Kills";

/** Return the rows of a table in the order 1997 listed them (Total Kills first). */
function inGameOrder(table: Table): number[] {
  const order = table.rows.map((_, index) => index);
  const total = table.rows.findIndex((r) => r[0] === TOTAL_FIRST);
  if (table.caption === "Summary Of Kills" && total > 0) {
    order.splice(total, 1);
    order.unshift(total);
  }
  return order;
}

/** Return the statistics page's rows, blank and heading rows included. */
export function statisticsRows(d: Debrief): StatRow[] {
  const rows: StatRow[] = [];
  statisticsTables(d).forEach((table, tableIndex) => {
    if (tableIndex === 0) {
      table.rows.forEach((cells, row) => {
        rows.push({
          kind: "line",
          text: `${YELLOW_CODE}${cells[0] ?? ""} ${DEFAULT}${cells[1] ?? ""}`,
          source: { table: 0, row, column: -1 },
        });
      });
      return;
    }
    if (table.caption !== "Accuracy") {
      rows.push({ kind: "blank" }, { kind: "heading", text: table.caption });
    }
    for (const row of inGameOrder(table)) {
      const cells = table.rows[row] ?? [];
      rows.push({
        kind: "pair",
        label: cells[0] ?? "",
        value: cells[1] ?? "",
        red: table.caption === "Craft Kills By Type",
        table: tableIndex,
        row,
      });
    }
  });
  return rows;
}

/** Return the statistics page's lines: the title and the 21 rows from `scroll`. */
export function statisticsLines(d: Debrief, scroll: number): DrawnLine[] {
  const lines: DrawnLine[] = [titleLine("statistics")];
  const rows = statisticsRows(d);
  const shown = rows.slice(scroll, scroll + STATISTICS.rows);
  shown.forEach((row, index) => {
    const y = STATISTICS.firstY + ROW * index;
    if (row.kind === "heading") {
      lines.push({
        font: 12,
        x: STATISTICS.labelX,
        y,
        ink: "yellow",
        text: row.text,
      });
    } else if (row.kind === "line") {
      lines.push({
        font: 12,
        x: STATISTICS.labelX,
        y,
        ink: "white",
        text: row.text,
        source: row.source,
      });
    } else if (row.kind === "pair") {
      lines.push(
        {
          font: 12,
          x: STATISTICS.labelX,
          y,
          ink: row.red ? "red" : "yellow",
          text: row.label,
          source: { table: row.table, row: row.row, column: 0 },
        },
        {
          font: 12,
          x: STATISTICS.valueX,
          y,
          ink: "white",
          text: row.value,
          source: { table: row.table, row: row.row, column: 1 },
        },
      );
    }
  });
  return lines;
}

/** Return how many rows the statistics page has, scrolling included. */
export function statisticsRowCount(d: Debrief): number {
  return statisticsRows(d).length;
}

/** Return the lines of ``tab`` (the statistics page from row ``scroll``). */
export function pageLines(
  d: Debrief,
  tab: DebriefTab,
  scroll: number,
): DrawnLine[] {
  return tab === "overview" ? overviewLines(d) : statisticsLines(d, scroll);
}
