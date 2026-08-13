import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { NWScriptCompiler } from "../../dist/index.mjs";

const projectRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");

export const fixturesDir = resolve(projectRoot, "test", "fixtures");
export const cliPath = resolve(projectRoot, "scripts", "compile-nss.mjs");

export async function readFixture(...parts) {
  return readFile(resolve(fixturesDir, ...parts), "utf8");
}

export async function languageSpec() {
  return readFixture("nwscript.nss");
}

export async function createCompiler(overrides = {}) {
  const spec = Object.prototype.hasOwnProperty.call(overrides, "languageSpec")
    ? overrides.languageSpec
    : await languageSpec();

  return NWScriptCompiler.create({
    ...overrides,
    languageSpec: spec,
  });
}
