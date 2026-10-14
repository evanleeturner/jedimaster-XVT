// Which of the game's twelve menu backgrounds the page sits on.
//
// Purpose: pick the frame picture the way the 1997 Mission Selection picks
// its background, from the shown mission's kind and the side of the shown
// team's briefing background.
// Flow: `teamKind` reads the side from a bundle's under-map art name;
// `frameBackground` applies the table; `frameArt` names the picture the
// frame route serves.
// Invariants: the mission's side stands in for the pilot's faction (the
// page has no pilot yet); a side that is neither rebel nor imperial, or no
// side at all, is neutral; melee is `gamemn` on every side; tournament,
// any other kind and no mission shown are `gametrn`.

export type Side = "rebel" | "imperial" | "neutral";

export const FRAME_PATH = "/art/frame/";

const SIDED: Readonly<Record<string, readonly [string, string, string]>> = {
  // [rebel, imperial, neutral]
  training: ["gametr", "gameti", "gametr"],
  combat: ["gamecr", "gameci", "gamecn"],
  battle: ["gamebr", "gamebi", "gamebn"],
  campaign: ["gamecar", "gamecai", "gamecar"],
  melee: ["gamemn", "gamemn", "gamemn"],
};

const UNDER_NAME = /^under-(rebel|imperial|neutral)\.png$/;

/** Return the side of an under-map art name (`under-rebel.png`), else neutral. */
export function teamKind(art: string | null): Side {
  const found = art === null ? null : UNDER_NAME.exec(art);
  if (found?.[1] === "rebel" || found?.[1] === "imperial") return found[1];
  return "neutral";
}

/** Return the background file's stem for a mission kind and a side. */
export function frameBackground(mission: string | null, side: Side): string {
  const row = mission === null ? undefined : SIDED[mission];
  if (row === undefined) return "gametrn";
  return row[side === "rebel" ? 0 : side === "imperial" ? 1 : 2];
}

/** Return the frame route's art name for background `background`. */
export function frameArt(background: string): string {
  return `frame-${background}.png`;
}
