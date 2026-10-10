// The heading the setup screen writes across the top of the map.
//
// Purpose: say which glyphs the heading draws: the heading string, a space,
// the yellow color byte and the name of the team whose briefing shows, in
// font 12, white, from panel (2, -4).
// Flow: `headingBytes` joins the parts into one byte string; `headingGlyphs`
// draws it with the heading font's metrics and the text rule of font 10.
// Invariants: the heading is drawn under the whole panel's clip, so only its
// rows from panel row 0 down show; a bundle with no heading font has no
// heading; text characters are bytes (the low 8 bits).

import type { BriefingBundle } from "../generated/briefing.ts";
import type { GlyphRecord } from "./records.ts";
import { drawBytes, toBytes } from "./text.ts";

export const HEADING_X = 2;
export const HEADING_Y = -4;
export const HEADING_COLOR = "white";
const SPACE = 32;
const TEAM_COLOR_BYTE = 4;

/**
 * Return the heading's bytes for `team`: the heading string, a space, the
 * color byte 4 (text color code 3, yellow) and the team's name. A team the
 * bundle does not list has an empty name.
 */
export function headingBytes(bundle: BriefingBundle, team: number): number[] {
  const name = bundle.teams.find((t) => t.team === team)?.name ?? "";
  return [...toBytes(bundle.heading), SPACE, TEAM_COLOR_BYTE, ...toBytes(name)];
}

/** Return the glyphs of `team`'s heading, or none when the bundle has no heading font. */
export function headingGlyphs(
  bundle: BriefingBundle,
  team: number,
): GlyphRecord[] {
  const font = bundle.heading_font;
  if (font === null) return [];
  return drawBytes(
    font,
    headingBytes(bundle, team),
    HEADING_COLOR,
    HEADING_X,
    HEADING_Y,
  ).glyphs;
}
