import { defineConfig } from "@playwright/test";

export default defineConfig({
  testDir: "e2e",
  outputDir: "test-results",
  fullyParallel: true,
  reporter: "list",
  timeout: 30_000,
  expect: { timeout: 10_000 },
  use: { browserName: "chromium" },
});
