// Which of the game's twelve menu backgrounds the page sits on.
//
// Purpose: pick the frame picture the way the 1997 Mission Selection picks
// its background, from the selected mission type and the pilot's chosen side.
// Flow: `frameBackground` applies the table; `frameArt` names the picture the
// frame route serves.
// Invariants: the side is the pilot's chosen side (the Rebel or Imperial pilot
// side button), not the side of the mission's team; `neutral` is the
// network game's column, which no solo page asks for; a side that is neither
// rebel nor imperial is neutral; melee is `gamemn` on every side;
// tournament, any other kind and no type selected are `gametrn`.

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

/** Return the background file's stem for a selected kind and the chosen side. */
export function frameBackground(mission: string | null, side: Side): string {
  const row = mission === null ? undefined : SIDED[mission];
  if (row === undefined) return "gametrn";
  return row[side === "rebel" ? 0 : side === "imperial" ? 1 : 2];
}

/** Return the frame route's art name for background `background`. */
export function frameArt(background: string): string {
  return `frame-${background}.png`;
}
