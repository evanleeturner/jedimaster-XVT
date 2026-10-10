import js from "@eslint/js";
import simpleImportSort from "eslint-plugin-simple-import-sort";
import { defineConfig } from "eslint/config";
import tseslint from "typescript-eslint";

export default defineConfig(
  {
    ignores: [
      "dist/",
      "node_modules/",
      "test-results/",
      "playwright-report/",
      "src/generated/",
    ],
  },
  {
    files: ["**/*.ts"],
    extends: [
      js.configs.recommended,
      tseslint.configs.strictTypeChecked,
      tseslint.configs.stylisticTypeChecked,
    ],
    languageOptions: {
      parserOptions: {
        projectService: true,
        tsconfigRootDir: import.meta.dirname,
      },
    },
    plugins: { "simple-import-sort": simpleImportSort },
    rules: {
      "simple-import-sort/imports": "error",
      "no-console": "error",
      "@typescript-eslint/no-floating-promises": [
        "error",
        {
          allowForKnownSafeCalls: [
            {
              from: "package",
              package: "node:test",
              name: ["test", "describe", "it", "suite"],
            },
          ],
        },
      ],
    },
  },
  { files: ["src/logger.ts"], rules: { "no-console": "off" } },
  { files: ["**/*.mjs"], extends: [tseslint.configs.disableTypeChecked] },
);
