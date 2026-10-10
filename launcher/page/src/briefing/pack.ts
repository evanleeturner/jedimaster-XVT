// Pack the panel's pixels the way the game's screen holds them.
//
// Purpose: turn 8-bit pixels read from a canvas into the 16-bit 5-6-5 values
// of the game's screen, so a page's panel can be compared with the game's
// own, exactly.
// Flow: `pack565` packs one color; `packPanel` packs a whole image's RGBA
// bytes into two bytes a pixel, low byte first.
// Invariants: red keeps its top 5 bits, green its top 6 and blue its top 5;
// alpha is ignored; the output is as long as twice the pixel count.

/** Return an 8-bit color as the 16-bit value `(r>>3)<<11 | (g>>2)<<5 | (b>>3)`. */
export function pack565(red: number, green: number, blue: number): number {
  return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
}

/** Return RGBA bytes packed to 5-6-5, two bytes a pixel, low byte first. */
export function packPanel(rgba: ArrayLike<number>): Uint8Array {
  const count = Math.floor(rgba.length / 4);
  const out = new Uint8Array(count * 2);
  for (let pixel = 0; pixel < count; pixel += 1) {
    const value = pack565(
      rgba[pixel * 4] ?? 0,
      rgba[pixel * 4 + 1] ?? 0,
      rgba[pixel * 4 + 2] ?? 0,
    );
    out[pixel * 2] = value & 0xff;
    out[pixel * 2 + 1] = value >> 8;
  }
  return out;
}
