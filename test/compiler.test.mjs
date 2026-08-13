import { afterEach, describe, expect, it } from "vitest";
import {
  NWScriptCompiler,
  OptimizationFlags,
} from "../dist/index.mjs";
import {
  createCompiler,
  languageSpec,
  readFixture,
} from "./helpers/compiler.mjs";

const decoder = new TextDecoder();

describe("NWScriptCompiler", () => {
  /** @type {import("../dist/index.mjs").default | undefined} */
  let compiler;

  afterEach(() => {
    compiler?.dispose();
    compiler = undefined;
  });

  it("throws TypeError without languageSpec or gameTarget", async () => {
    await expect(NWScriptCompiler.create()).rejects.toBeInstanceOf(TypeError);
    await expect(NWScriptCompiler.create({})).rejects.toBeInstanceOf(TypeError);
  });

  it("throws RangeError for an unknown gameTarget", async () => {
    await expect(
      NWScriptCompiler.create({ gameTarget: "nope" }),
    ).rejects.toBeInstanceOf(RangeError);
  });

  it("lets languageSpec win over gameTarget", async () => {
    compiler = await createCompiler({ gameTarget: "nope" });
    const result = compiler.compile("hello", await readFixture("scripts", "hello.nss"));
    expect(result.ok).toBe(true);
  });

  it("compiles a valid script to NCS bytecode", async () => {
    compiler = await createCompiler();
    const result = compiler.compile("hello", await readFixture("scripts", "hello.nss"));
    expect(result.ok).toBe(true);
    expect(result.code).toBe(0);
    expect(result.error).toBe("");
    expect(result.bytecode.byteLength).toBeGreaterThan(13);
    expect(decoder.decode(result.bytecode.subarray(0, 8))).toBe("NCS V1.0");
  });

  it("returns a failed result for a syntax error", async () => {
    compiler = await createCompiler();
    const result = compiler.compile(
      "syntax_error",
      await readFixture("scripts", "syntax_error.nss"),
    );
    expect(result.ok).toBe(false);
    expect(result.code).not.toBe(0);
    expect(result.error.length).toBeGreaterThan(0);
    expect(result.bytecode.byteLength).toBe(0);
  });

  it("emits debugCode when writeDebug is enabled", async () => {
    compiler = await createCompiler({ writeDebug: true });
    const result = compiler.compile("hello", await readFixture("scripts", "hello.nss"));
    expect(result.ok).toBe(true);
    expect(result.debugCode.byteLength).toBeGreaterThan(0);
  });

  it("toggles debugger output with setDebugOutput", async () => {
    compiler = await createCompiler({ writeDebug: false });
    const source = await readFixture("scripts", "hello.nss");

    compiler.setDebugOutput(false);
    const withoutDebug = compiler.compile("hello", source);
    expect(withoutDebug.ok).toBe(true);
    expect(withoutDebug.debugCode.byteLength).toBe(0);

    compiler.setDebugOutput(true);
    const withDebug = compiler.compile("hello", source);
    expect(withDebug.ok).toBe(true);
    expect(withDebug.debugCode.byteLength).toBeGreaterThan(0);
  });

  it("produces smaller or equal bytecode at O3 than O0", async () => {
    compiler = await createCompiler({
      optimizationFlags: OptimizationFlags.O0,
    });
    const source = await readFixture("scripts", "dead.nss");
    const o0 = compiler.compile("dead", source);
    expect(o0.ok).toBe(true);

    compiler.setOptimizationFlags(OptimizationFlags.O3);
    const o3 = compiler.compile("dead", source);
    expect(o3.ok).toBe(true);
    expect(o3.bytecode.byteLength).toBeLessThanOrEqual(o0.bytecode.byteLength);
  });

  it("accepts Uint8Array language spec and source", async () => {
    const spec = new TextEncoder().encode(await languageSpec());
    compiler = await createCompiler({ languageSpec: spec });
    const source = new TextEncoder().encode(await readFixture("scripts", "hello.nss"));
    const result = compiler.compile("hello", source);
    expect(result.ok).toBe(true);
  });

  it("normalizes script names as resrefs", async () => {
    compiler = await createCompiler();
    const source = await readFixture("scripts", "hello.nss");
    compiler.addSource("hello", source);
    const byFile = compiler.compile("Hello.nss");
    const byCase = compiler.compile("HELLO");
    expect(byFile.ok).toBe(true);
    expect(byCase.ok).toBe(true);
  });

  it("throws after dispose and allows a second dispose", async () => {
    compiler = await createCompiler();
    compiler.dispose();
    expect(() => compiler.compile("hello", "void main() {}")).toThrow(
      /disposed/i,
    );
    expect(() => compiler.dispose()).not.toThrow();
    compiler = undefined;
  });

  it("lists embedded game targets as strings", async () => {
    const targets = await NWScriptCompiler.getEmbeddedGameTargets();
    expect(Array.isArray(targets)).toBe(true);
    for (const target of targets) {
      expect(typeof target).toBe("string");
      expect(target.length).toBeGreaterThan(0);
    }

    if (targets.length > 0) {
      compiler = await NWScriptCompiler.create({ gameTarget: targets[0] });
      const result = compiler.compile("hello", await readFixture("scripts", "hello.nss"));
      expect(result.ok).toBe(true);
    }
  });
});
