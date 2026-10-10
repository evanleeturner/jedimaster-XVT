// CRC-32 as zlib computes it: polynomial 0xEDB88320, start and end 0xFFFFFFFF.

const TABLE: readonly number[] = Array.from({ length: 256 }, (_, n) => {
  let value = n;
  for (let bit = 0; bit < 8; bit += 1) {
    value = (value & 1) === 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
  }
  return value >>> 0;
});

/** Return the CRC-32 of `bytes` as an unsigned 32-bit number. */
export function crc32(bytes: Uint8Array): number {
  let value = 0xffffffff;
  for (const byte of bytes) {
    value = (TABLE[(value ^ byte) & 0xff] ?? 0) ^ (value >>> 8);
  }
  return (value ^ 0xffffffff) >>> 0;
}

/** Return the CRC-32 of `text`, each character taken as one byte (0 to 255). */
export function crc32OfText(text: string): number {
  const bytes = new Uint8Array(text.length);
  for (let i = 0; i < text.length; i += 1) {
    bytes[i] = text.charCodeAt(i) & 0xff;
  }
  return crc32(bytes);
}

/** Return `value` as 8 lowercase hex digits. */
export function hex8(value: number): string {
  return value.toString(16).padStart(8, "0");
}
