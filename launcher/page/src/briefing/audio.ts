// Sound for the briefing player, through Web Audio.
//
// Purpose: play the setup screen's sounds (target, text, button press) from
// the page's own server, and only after the player has pressed a control.
// Flow: `unlock` (called from a control's press) creates the audio context;
// `play` fetches and decodes a sound the first time, then starts it; a sound
// that cannot be loaded is skipped with a warning.
// Invariants: nothing is created or fetched before `unlock`; `play` does
// nothing while sound is off or before `unlock`; one decode per sound.

import type { BriefingBundle } from "../generated/briefing.ts";
import { logger } from "../logger.ts";
import { ART_PATH } from "./sprites.ts";

export const BUTTON_SOUND = "jewelsound";

export class Sounds {
  readonly #files: ReadonlyMap<string, string>;
  readonly #buffers = new Map<string, Promise<AudioBuffer | null>>();
  #context: AudioContext | null = null;
  on = true;

  constructor(bundle: BriefingBundle) {
    this.#files = new Map(
      bundle.sounds.map((sound) => [sound.name, sound.picture]),
    );
  }

  /** Create the audio context; call it from a press, never before. */
  unlock(): void {
    if (this.#context !== null || typeof AudioContext === "undefined") return;
    this.#context = new AudioContext();
    logger.info("sound unlocked");
  }

  async #load(name: string): Promise<AudioBuffer | null> {
    const file = this.#files.get(name);
    const context = this.#context;
    if (file === undefined || context === null) return null;
    try {
      const reply = await fetch(`${ART_PATH}${file}`);
      if (!reply.ok) return null;
      return await context.decodeAudioData(await reply.arrayBuffer());
    } catch (error) {
      logger.warn("a briefing sound could not be loaded", name, error);
      return null;
    }
  }

  /** Play the sound called `name` once, if sound is on and unlocked. */
  play(name: string): void {
    const context = this.#context;
    if (!this.on || context === null) return;
    let buffer = this.#buffers.get(name);
    if (buffer === undefined) {
      buffer = this.#load(name);
      this.#buffers.set(name, buffer);
    }
    void buffer.then((decoded) => {
      if (decoded === null) return;
      const source = context.createBufferSource();
      source.buffer = decoded;
      source.connect(context.destination);
      source.start();
    });
  }
}
