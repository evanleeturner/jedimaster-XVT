// The panel's colors, seen through the game's 16-bit 565 screen.
//
// Purpose: turn a record's color name into the 8-bit color the screen shows,
// and tint a grey icon pixel.
// Flow: `colorRgb` maps a name (`shade N`, `white`, `yellow`, `major`,
// `minor`, `codeN`) to an exact color and passes it through 565; `tintRgb`
// scales a tint by a pixel's intensity (0 to 31) and passes it through 565.
// Invariants: 565 keeps 5 bits of red, 6 of green and 5 of blue; a 565 color
// widens back by repeating its top bits; the 40 shades are rows of 8 steps
// (green, red, yellow, blue, purple).

export type Rgb = readonly [number, number, number];

const STEPS = [0x48, 0x60, 0x78, 0x94, 0xac, 0xc8, 0xe0, 0xfc] as const;
const ROWS: readonly (readonly [boolean, boolean, boolean])[] = [
  [false, true, false],
  [true, false, false],
  [true, true, false],
  [false, false, true],
  [true, false, true],
];
const FIXED: Readonly<Record<string, Rgb>> = {
  white: [255, 255, 255],
  yellow: [255, 255, 0],
  major: [0x96, 0, 0],
  minor: [0x50, 0, 0],
};
const BLACK: Rgb = [0, 0, 0];
export const FULL_INTENSITY = 31;

/** Return `rgb` as the 565 screen shows it, widened back to 8 bits per channel. */
export function through565(rgb: Rgb): Rgb {
  const red = rgb[0] >> 3;
  const green = rgb[1] >> 2;
  const blue = rgb[2] >> 3;
  return [
    (red << 3) | (red >> 2),
    (green << 2) | (green >> 4),
    (blue << 3) | (blue >> 2),
  ];
}

/** Return a 16-bit 565 value as 8-bit channels. */
export function fromColor565(value: number): Rgb {
  const red = (value >> 11) & 31;
  const green = (value >> 5) & 63;
  const blue = value & 31;
  return [
    (red << 3) | (red >> 2),
    (green << 2) | (green >> 4),
    (blue << 3) | (blue >> 2),
  ];
}

/** Return the exact color a name stands for, before the 565 screen. */
export function exactColor(name: string, textColors: readonly number[]): Rgb {
  const fixed = FIXED[name];
  if (fixed !== undefined) return fixed;
  const shade = /^shade (\d+)$/.exec(name);
  if (shade !== null) {
    const number = Number(shade[1]);
    const row = ROWS[number >> 3];
    const step = STEPS[number & 7];
    if (row === undefined || step === undefined) return BLACK;
    return [row[0] ? step : 0, row[1] ? step : 0, row[2] ? step : 0];
  }
  const code = /^code(\d)$/.exec(name);
  if (code !== null) {
    const value = textColors[Number(code[1]) - 1];
    return value === undefined ? BLACK : fromColor565(value);
  }
  return BLACK;
}

/** Return the color a name shows on the 565 screen. */
export function colorRgb(name: string, textColors: readonly number[]): Rgb {
  const fixed = exactColor(name, textColors);
  return name.startsWith("code") ? fixed : through565(fixed);
}

/** Return `tint` scaled by `intensity` (0 to 31), as the 565 screen shows it. */
export function tintRgb(tint: Rgb, intensity: number): Rgb {
  const scale = (channel: number): number =>
    Math.trunc((channel * intensity) / FULL_INTENSITY);
  return through565([scale(tint[0]), scale(tint[1]), scale(tint[2])]);
}

/** Return a pixel's intensity: the blue field (0 to 31) of its color in 565. */
export function intensityOf(blue8: number): number {
  return blue8 >> 3;
}
