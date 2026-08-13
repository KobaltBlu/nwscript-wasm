import { afterEach, describe, expect, it } from "vitest";
import { createCompiler, readFixture } from "./helpers/compiler.mjs";

describe("source registration", () => {
  /** @type {import("../dist/index.mjs").default | undefined} */
  let compiler;

  afterEach(() => {
    compiler?.dispose();
    compiler = undefined;
  });

  it("compiles a script that includes an added source", async () => {
    compiler = await createCompiler();
    compiler.addSource("inc_helper", await readFixture("scripts", "inc_helper.nss"));
    const result = compiler.compile(
      "needs_include",
      await readFixture("scripts", "needs_include.nss"),
    );
    expect(result.ok).toBe(true);
    expect(result.bytecode.byteLength).toBeGreaterThan(13);
  });

  it("registers multiple includes with addSources", async () => {
    compiler = await createCompiler();
    compiler.addSources({
      inc_helper: await readFixture("scripts", "inc_helper.nss"),
      extra: "void Extra() {}\n",
    });
    const result = compiler.compile(
      "needs_include",
      await readFixture("scripts", "needs_include.nss"),
    );
    expect(result.ok).toBe(true);
  });

  it("returns true then false from removeSource and throws on an empty name", async () => {
    compiler = await createCompiler();
    compiler.addSource("inc_helper", await readFixture("scripts", "inc_helper.nss"));
    expect(compiler.removeSource("inc_helper")).toBe(true);
    expect(compiler.removeSource("inc_helper")).toBe(false);
    expect(() => compiler.removeSource("")).toThrow();
  });

  it("fails after clearSources removes a required include", async () => {
    compiler = await createCompiler();
    const includeText = await readFixture("scripts", "inc_helper.nss");
    const source = await readFixture("scripts", "needs_include.nss");

    compiler.addSource("inc_helper", includeText);
    expect(compiler.compile("needs_include", source).ok).toBe(true);

    compiler.clearSources();
    compiler.addSource("needs_include", source);
    const result = compiler.compile("needs_include");
    expect(result.ok).toBe(false);
  });

  it("reports a missing include in the compile error", async () => {
    compiler = await createCompiler();
    const result = compiler.compile(
      "needs_include",
      await readFixture("scripts", "needs_include.nss"),
    );
    expect(result.ok).toBe(false);
    expect(result.error.toLowerCase()).toMatch(/inc_helper/);
  });
});
