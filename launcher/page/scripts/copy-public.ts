import { copyFile, mkdir } from "node:fs/promises";
import { join } from "node:path";

const FILES = ["index.html", "style.css"];

await mkdir("dist", { recursive: true });
for (const name of FILES) {
  await copyFile(join("public", name), join("dist", name));
}
