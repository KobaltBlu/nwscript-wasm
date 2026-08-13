import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { createCompiler } from "./helpers/compiler.mjs";

const projectRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");

function readProject(...parts) {
  return readFileSync(resolve(projectRoot, ...parts), "utf8");
}

function collectCppExports(source) {
  const names = new Set();
  for (const line of source.split(/\r?\n/)) {
    if (!/NWSC_(?:EXPORT|DISASM_EXPORT|EMBED_EXPORT|NDB_EXPORT)/.test(line)) {
      continue;
    }
    const match = line.match(/\b(nwsc_\w+)\s*\(/);
    if (match) {
      names.add(match[1]);
    }
  }
  return names;
}

describe("WASM/JS ABI", () => {
  const cppExports = new Set([
    ...collectCppExports(readProject("compiler_wasm.cpp")),
    ...collectCppExports(readProject("ncs_disassembler.cpp")),
    ...collectCppExports(readProject("ndb_inspector.cpp")),
    ...collectCppExports(readProject("scripts", "generate-game-targets.mjs")),
  ]);

  const buildSource = readProject("scripts", "build.mjs");
  const buildExports = new Set(
    [...buildSource.matchAll(/"(_(?:nwsc_\w+|malloc|free))"/g)].map((match) => match[1]),
  );

  const jsSource = readProject("index.mjs");
  const jsCalls = new Set(
    [...jsSource.matchAll(/\b(_nwsc_\w+|_malloc|_free)\b/g)].map((match) => match[1]),
  );

  const unusedFromJs = new Set(["_nwsc_last_code"]);

  it("exports every C++ nwsc_* symbol from Emscripten", () => {
    const prefixed = [...cppExports].map((name) => `_${name}`).sort();
    const fromBuild = [...buildExports].filter((name) => name.startsWith("_nwsc_")).sort();
    expect(fromBuild).toEqual(prefixed);
  });

  it("lists malloc and free in EXPORTED_FUNCTIONS", () => {
    expect(buildExports.has("_malloc")).toBe(true);
    expect(buildExports.has("_free")).toBe(true);
  });

  it("calls every exported nwsc_* hook except the last_code allowlist", () => {
    const expected = [...buildExports].filter((name) => !unusedFromJs.has(name)).sort();
    const called = [...jsCalls].sort();
    expect(called).toEqual(expected);
  });

  it("does not call undeclared WASM exports", () => {
    for (const name of jsCalls) {
      expect(buildExports.has(name)).toBe(true);
    }
  });

  it("accepts ABI version 1 when creating a compiler", async () => {
    const compiler = await createCompiler();
    try {
      expect(compiler).toBeTruthy();
    } finally {
      compiler.dispose();
    }
  });
});
